/*
 * ulock_kthread_test.c - real blocking-wait/wake smoke test for
 * third_party/xnu-7195/bsd/kern/sys_ulock.c's ulock_wait(2)/ulock_wake(2)
 * (syscalls 515/516, UL_COMPARE_AND_WAIT op), the real Apple-open-source
 * kernel backing for os_unfair_lock and (per libpthread's own
 * PTHREAD_MUTEX_USE_ULOCK=1 knob, see third_party/libpthread/src/
 * pthread_mutex.c's _pthread_mutex_global_init()) an alternate,
 * non-psynch, real userspace-pthread_mutex/pthread_cond fast path -- see
 * docs/roadmap.md Phase 7.5 for why this matters: psynch_mutexwait/
 * psynch_cvwait's actual protocol logic was never open-sourced (only the
 * generic turnstile/waitq primitives it would call were), but sys_ulock.c
 * is unmodified, real, already-open-source Apple code that does NOT touch
 * the closed pthread_functions vtable at all (confirmed by grepping it for
 * pthread_kern/pthread_functions - zero hits) and was already compiled
 * into this kernel (bsd/conf/files: "bsd/kern/sys_ulock.c standard") the
 * whole time, just never exercised by anything in this project's
 * userland before this test.
 *
 * Unlike tools/pthread_kthread_test/ and tools/workq_kthread_test/ (which
 * both busy-poll a shared word because no real blocking wait/wake existed
 * yet), this test proves a REAL blocking handoff: the main thread calls
 * ulock_wait(2) and actually blocks in the kernel (real
 * waitq_assert_wait64/thread_block, real turnstile_prepare) until the
 * child thread calls ulock_wake(2) (real waitq_wakeup64_thread) or a
 * bounded timeout fires. Race-safe by construction: if the child's write+
 * wake happens before the main thread's wait call, ulock_wait's own
 * copyin-under-lock check (sys_ulock.c's "value != args->value: bail out"
 * path) sees the word already changed and returns immediately without
 * blocking - either ordering ends with lockword == MAGIC and the test
 * passing.
 *
 * Second thread creation reuses the already-real, already-QEMU-verified
 * bsdthread_create() path from bsd_pthread_stub.c (see
 * pthread_kthread_test.c for the fuller explanation of that mechanism) -
 * this test only adds real synchronization on top of it, raw syscall,
 * same freestanding/no-libc/carry-flag-errno shape as every other test in
 * tools/.
 */
typedef unsigned long size_t;
typedef long ssize_t;
typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;
typedef long int64_t;

/* ---------------- raw syscalls (arm64 Darwin: svc #0x80, num in x16) ---- */

#define SYS_exit             1
#define SYS_write             4
#define SYS_bsdthread_create 360
#define SYS_ulock_wait       515
#define SYS_ulock_wake       516

/* UL_COMPARE_AND_WAIT: pure futex-style wait/wake, no mach-port owner
 * semantics (that's UL_UNFAIR_LOCK only) - see bsd/sys/ulock.h. */
#define UL_COMPARE_AND_WAIT 1
#define ULF_NO_ERRNO        0x01000000

static inline long syscall5(long num, long a0, long a1, long a2, long a3, long a4) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x3 asm("x3") = a3;
    register long x4 asm("x4") = a4;
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        return -x0;
    }
    return x0;
}

static inline long syscall3(long num, long a0, long a1, long a2) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x2), "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        return -x0;
    }
    return x0;
}

static void put(const char *s) {
    size_t n = 0;
    while (s[n]) {
        n++;
    }
    syscall3(SYS_write, 1, (long)s, (long)n);
}

static void put_dec(long v) {
    char buf[24];
    int i = 22;
    int neg = 0;
    unsigned long uv;
    buf[23] = 0;
    if (v < 0) {
        neg = 1;
        uv = (unsigned long)(-v);
    } else {
        uv = (unsigned long)v;
    }
    if (uv == 0) {
        buf[i--] = '0';
    }
    while (uv) {
        buf[i--] = (char)('0' + (uv % 10));
        uv /= 10;
    }
    if (neg) {
        buf[i--] = '-';
    }
    put(&buf[i + 1]);
}

static void sys_exit(int code) {
    syscall3(SYS_exit, code, 0, 0);
    for (;;) { }
}

/* ulock_wait(operation, addr, value, timeout_us) -> 0/positive on wake,
 * negative errno (ETIMEDOUT etc, via the carry-flag convention) on
 * failure. Bounded timeout (not 0/infinite) deliberately: this is the
 * first time this kernel's turnstile/waitq path is exercised from a real
 * ulock syscall, so a kernel-side bug should show up as a diagnosable
 * TIMEOUT rather than hang QEMU forever. */
static long ulock_wait(uint32_t operation, volatile uint32_t *addr, uint64_t value,
                        uint32_t timeout_us) {
    return syscall5(SYS_ulock_wait, (long)operation, (long)addr, (long)value,
                     (long)timeout_us, 0);
}

/* ------------------------ the second thread's entry ---------------------
 * Raw asm, no C ABI - x0 = func_arg = &lockword (bsdthread_create's
 * contract, see pthread_kthread_test.c). Writes MAGIC, then calls
 * ulock_wake(2) (syscall 516) directly in asm to wake the main thread's
 * real blocking ulock_wait, then spins forever (task torn down by the
 * main thread's exit(2), same as pthread_kthread_test.c).
 */
#define MAGIC 0x1234abcdU

__attribute__((naked)) static void child_entry(void) {
    asm volatile(
        /* UL_COMPARE_AND_WAIT operates on a 32-bit word (sys_ulock.c's
         * ulock_wait2: lock_size = sizeof(uint32_t)/copyin_atomic32) --
         * lockword is uint32_t and this must be a 32-bit store (w1, not
         * x1) to match, or a MAGIC value with any bit above bit 31 set
         * would silently only compare/store the low half. */
        "mov  w1, #0xabcd\n"
        "movk w1, #0x1234, lsl #16\n"
        "str  w1, [x0]\n"
        /* ulock_wake(UL_COMPARE_AND_WAIT | ULF_NO_ERRNO, &lockword, 0) */
        "mov  x19, x0\n"            /* save &lockword (x0 clobbered by syscall) */
        "mov  w0, #1\n"             /* UL_COMPARE_AND_WAIT */
        "movk w0, #0x100, lsl #16\n"/* | ULF_NO_ERRNO (0x01000000) */
        "mov  x1, x19\n"
        "mov  x2, #0\n"
        "mov  x16, #516\n"          /* SYS_ulock_wake */
        "svc  #0x80\n"
        "1: b 1b\n"
    );
}

#define CHILD_STACK_SIZE (16 * 1024)
__attribute__((used)) static char child_stack[CHILD_STACK_SIZE];

#define SPIN_LIMIT 200000000UL
#define WAIT_TIMEOUT_US (5U * 1000U * 1000U) /* 5s bound */

static volatile uint32_t lockword;

__attribute__((used)) static void main_test(void) {
    long r;
    unsigned long stack_top;
    unsigned long i;

    put("ulock_kthread_test: starting\n");

    lockword = 0;

    stack_top = (unsigned long)child_stack + CHILD_STACK_SIZE;
    stack_top &= ~(unsigned long)15;

    r = syscall5(SYS_bsdthread_create,
                 (long)(unsigned long)child_entry,
                 (long)&lockword,
                 (long)stack_top,
                 0, 0);
    if (r < 0) {
        put("bsdthread_create FAILED, errno=");
        put_dec(-r);
        put("\n");
        sys_exit(1);
    }

    put("bsdthread_create returned, blocking in real ulock_wait...\n");

    r = ulock_wait(UL_COMPARE_AND_WAIT | ULF_NO_ERRNO, &lockword, 0, WAIT_TIMEOUT_US);
    if (r < 0) {
        put("ulock_wait FAILED, errno=");
        put_dec(-r);
        put("\n");
        sys_exit(1);
    }
    put("ulock_wait returned, ret=");
    put_dec(r);
    put("\n");

    /* Whether we actually blocked-then-woke or bailed out immediately
     * because the child had already updated lockword (see header comment
     * on race-safety), the only thing that proves success is the value. */
    for (i = 0; i < SPIN_LIMIT; i++) {
        if (lockword == MAGIC) {
            put("SUCCESS: real ulock_wait/ulock_wake handoff observed\n");
            sys_exit(0);
        }
    }

    put("TIMEOUT: lockword never observed set after ulock_wait returned\n");
    sys_exit(1);
}

#define STACK_SIZE (64 * 1024)
__attribute__((used)) static char boot_stack[STACK_SIZE];

__attribute__((naked)) void _start(void) {
    asm volatile(
        "adrp x0, _boot_stack@PAGE\n"
        "add  x0, x0, _boot_stack@PAGEOFF\n"
        "add  x0, x0, %0\n"
        "and  sp, x0, #~15\n"
        "bl   _main_test\n"
        "mov  x0, #0\n"
        "mov  x16, #1\n"       /* SYS_exit */
        "svc  #0x80\n"
        "1: b 1b\n"
        :
        : "i"(STACK_SIZE)
        : "x0"
    );
}
