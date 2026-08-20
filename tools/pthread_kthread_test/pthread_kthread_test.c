/*
 * pthread_kthread_test.c - smallest real, observable exercise of
 * third_party/xnu-7195/bsd/kern/bsd_pthread_stub.c's new real
 * bsdthread_create() (Phase 7.5's first real-pthread step, see
 * docs/roadmap.md).
 *
 * NOT a pthread_create()-compatible test: there is no real libpthread in
 * this project's userland yet (that would need real libpthread's
 * trampoline/TSD wiring, itself real, substantial follow-on work - see
 * bsd_pthread_stub.c's header comment). This calls the raw bsdthread_create
 * BSD syscall (number 360, from third_party/xnu-7195/bsd/kern/
 * syscalls.master line 541, not guessed) directly, exactly the way
 * tools/init_binary/init.c already calls every other raw syscall it uses:
 * freestanding, no libc, `svc #0x80` with the syscall number in x16, arm64
 * Darwin's positive-errno+carry-flag error convention (see init.c's own
 * header comment for the real, QEMU-monitor-confirmed root cause of why
 * that convention matters here).
 *
 * Test design (see bsd_pthread_stub.c's header comment for exactly what
 * register state the new thread starts with - pc/sp/x0 only, nothing
 * else): the main thread creates a second real Mach thread whose entry
 * point (child_entry, raw asm, no C ABI) writes a fixed magic value to an
 * address the main thread also has (passed in as bsdthread_create's
 * func_arg, landing in the new thread's x0), then spins forever. The main
 * thread busy-polls that address for the magic value, reports
 * success/failure, then calls the ordinary exit(2) syscall - which tears
 * down the WHOLE task, including the still-spinning second thread. That
 * final step is itself a real, first-time-for-this-project exercise of
 * XNU's own task_terminate_internal() reaping >1 live thread in a task;
 * this test's own commit message + docs/roadmap.md record whether that
 * was confirmed clean on real QEMU boot.
 */
typedef unsigned long size_t;
typedef long ssize_t;
typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;
typedef long int64_t;

/* ---------------- raw syscalls (arm64 Darwin: svc #0x80, num in x16) ---- */

#define SYS_exit            1
#define SYS_write            4
#define SYS_bsdthread_create 360

/* Same carry-flag-on-error convention as tools/init_binary/init.c's
 * syscall3 (see that file's header comment for the real root cause this
 * was found from) - repeated here with a 5th argument register (x4) since
 * bsdthread_create takes 5 user args (func, func_arg, stack, pthread,
 * flags), more than init.c's syscall3 wrapper passes. */
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

/* Minimal signed-decimal printer -- avoids pulling in any real itoa/printf
 * for a one-off diagnostic value (the raw errno on failure). */
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

/* ------------------------ the second thread's entry ---------------------
 * Raw asm, no C ABI: this is exactly what bsdthread_create() hands
 * control to (pc = this address, x0 = func_arg = &flag, sp = the stack
 * this test set up below) - see bsd_pthread_stub.c's real_bsdthread_create.
 * Writes a fixed 32-bit magic value (zero-extended into the full 64-bit
 * word `flag` the main thread reads) to [x0], then spins forever; the
 * whole task (both threads) is torn down by the main thread's exit(2)
 * once it observes the magic value, not by this thread returning (it
 * never does - there is nothing for it to return to; no real pthread ABI
 * here yet, see header comment).
 */
__attribute__((naked)) static void child_entry(void) {
    asm volatile(
        "mov  x1, #0xabcd\n"
        "movk x1, #0x1234, lsl #16\n"
        "str  x1, [x0]\n"
        "1: b 1b\n"
    );
}

#define CHILD_STACK_SIZE (16 * 1024)
__attribute__((used)) static char child_stack[CHILD_STACK_SIZE];

#define MAGIC 0x1234abcdUL
#define SPIN_LIMIT 200000000UL

static volatile long flag;

__attribute__((used)) static void main_test(void) {
    long r;
    unsigned long stack_top;
    unsigned long i;

    put("pthread_kthread_test: starting\n");

    flag = 0;

    stack_top = (unsigned long)child_stack + CHILD_STACK_SIZE;
    stack_top &= ~(unsigned long)15; /* 16-byte SP alignment, AArch64 ABI */

    r = syscall5(SYS_bsdthread_create,
                 (long)(unsigned long)child_entry,
                 (long)&flag,
                 (long)stack_top,
                 0 /* user_pthread, unused by this minimal path */,
                 0 /* flags, unused by this minimal path */);

    if (r < 0) {
        put("bsdthread_create FAILED, errno=");
        put_dec(-r);
        put("\n");
        sys_exit(1);
    }

    put("bsdthread_create returned, waiting for second thread...\n");

    for (i = 0; i < SPIN_LIMIT; i++) {
        if (flag == (long)MAGIC) {
            put("SUCCESS: second real Mach thread ran and wrote the magic value\n");
            sys_exit(0);
        }
    }

    put("TIMEOUT: second thread never observed to run\n");
    sys_exit(1);
}

/* ------------------------------- entry point -------------------------------
 * Same pattern as tools/init_binary/init.c's _start: never trust the
 * kernel's LC_UNIXTHREAD sp, switch to our own static stack first.
 */
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
