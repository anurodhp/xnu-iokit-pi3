/*
 * workq_kthread_test.c - smallest real, observable exercise of the five
 * real workq_* thread-stack hooks + real_bsdthread_register just landed
 * in third_party/xnu-7195/bsd/kern/bsd_pthread_workq.c (Phase 7.5, see
 * docs/roadmap.md) - ported real Apple pthread.kext kernel-support code
 * (third_party/libpthread/kern/kern_support.c, pinned libpthread-454.80.2)
 * rather than hand-rolled stubs, same real-primitives standard as
 * tools/pthread_kthread_test/ used for bsdthread_create() itself.
 *
 * What this proves, and how (raw syscalls only - freestanding, no libc,
 * same style as every other tools test binary in this project):
 *
 *   1. bsdthread_register(2) (syscall 366): registers this process's real
 *      wqthread entry point (wq_entry below) via the OLDER
 *      `.bsdthread_register` pthread_functions vtable slot (see
 *      bsd_pthread_workq.c's header comment for why that's the real,
 *      verified-correct slot for this project's exact xnu-7195 +
 *      libpthread-454.80.2 pairing) - real_bsdthread_register() ->
 *      pthread_kern->proc_set_wqthread()/proc_set_threadstart()/
 *      proc_set_register(). Passed with stack_addr_hint=0 (so
 *      real_bsdthread_register takes its simple, non-blob
 *      `pthread_init_data == 0` path - no _pthread_registration_data
 *      copyin/copyout needed for this minimal test) and flags=0 (which
 *      real_bsdthread_register receives as its `pthsize` parameter, per
 *      the real, evidence-backed old-slot argument mapping documented in
 *      bsd_pthread_workq.c - 0 is a valid pthsize, just means "no extra
 *      pthread_t-sized region beyond the real PTHREAD_T_OFFSET").
 *   2. workq_open(2) (syscall 367): real bsd/pthread/pthread_workqueue.c
 *      workq_open() - allocates this process's struct workqueue. Its own
 *      precondition (P_LREGISTER set) is satisfied by step 1's real
 *      proc_set_register() call.
 *   3. workq_kernreturn(2) (syscall 368) with WQOPS_QUEUE_REQTHREADS:
 *      real workq_reqthreads() -> workq_schedule_creator() ->
 *      (no existing idle/creator thread) -> workq_add_new_idle_thread()
 *      -> real workq_create_threadstack() (real mach_vm_map_external +
 *      mach_vm_protect guard page in THIS process's own vm_map) + real,
 *      already-pre-existing osfmk/kern/thread.c
 *      thread_create_workq_waiting() (confirmed unmodified/pre-existing
 *      this session, not part of this port) -> back in
 *      workq_schedule_creator(), the freshly-idle thread is popped and
 *      woken for real (workq_thread_wakeup()) - which is what actually
 *      runs real workq_setup_thread() on that second real Mach thread's
 *      own return-to-userspace path, landing it at wq_entry below with
 *      the real arm64 wqthread register convention (x0=self, x1=kport,
 *      x2=stack_bottom, x3=kevent_list, x4=upcall_flags,
 *      x5=kevent_count, sp=stack_top) - see
 *      bsd_pthread_workq.c:workq_set_register_state().
 *
 * `pp` (the real `prio` argument, a real pthread_priority_t / QOS
 * encoding) is a fixed, hand-derived constant, not a magic number:
 * `_pthread_priority_make_from_thread_qos(THREAD_QOS_LEGACY, 0, 0)`
 * (third_party/xnu-7195/bsd/pthread/priority_private.h) evaluates to
 * `(1 << (8 + 4 - 1)) | (((uint8_t)0 - 1) & 0xff)` = 0x800 | 0xff =
 * 0x8FF - verified by hand-tracing _pthread_priority_thread_qos() back
 * against it (round-trips to THREAD_QOS_LEGACY=4), see
 * docs/roadmap.md's Phase 7.5 entry for the full derivation.
 *
 * The new thread's real, kernel-allocated stack (from
 * workq_create_threadstack(), NOT a buffer this test controls, unlike
 * tools/pthread_kthread_test/'s bsdthread_create()-based test) means
 * wq_entry cannot be handed a pointer to this test's `flag` global via
 * any register - x0 is "self" (a pthread_t address inside the new
 * stack region), not a caller-supplied arg. wq_entry instead computes
 * `&flag`'s address itself via ordinary PC-relative adrp/add (it's
 * running in the same process address space, so this project's own
 * static data is just as reachable from the new thread as from the
 * main one), writes the magic value, and parks itself in a spin loop -
 * it does NOT call workq_kernreturn(WQOPS_THREAD_RETURN) to properly
 * park back into the kernel (that would be real follow-on work, out of
 * scope here - see bsd_pthread_workq.c's own NOT-ported-yet list). The
 * main thread polls `flag`, reports success/timeout, then calls
 * ordinary exit(2) - tearing down the whole task including the still-
 * spinning workqueue thread, the same real task_terminate_internal()
 * multi-thread reap path tools/pthread_kthread_test/ already exercised.
 */
typedef unsigned long size_t;
typedef long ssize_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;

#define SYS_exit               1
#define SYS_write              4
#define SYS_bsdthread_register 366
#define SYS_workq_open         367
#define SYS_workq_kernreturn   368

/* WQOPS_QUEUE_REQTHREADS - bsd/pthread/workqueue_syscalls.h line 40 */
#define WQOPS_QUEUE_REQTHREADS 0x020

/* Same carry-flag-on-error convention as tools/init_binary/init.c's
 * syscall3 / tools/pthread_kthread_test/'s syscall5 (see either file's
 * header comment for the real arm64 Darwin ABI root cause this matters
 * for on this project). */
static inline long syscall0(long num) {
    register long x0 asm("x0");
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "=r"(x0), "=r"(is_error)
                 : "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        return -x0;
    }
    return x0;
}

static inline long syscall4(long num, long a0, long a1, long a2, long a3) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x3 asm("x3") = a3;
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x2), "r"(x3), "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        return -x0;
    }
    return x0;
}

static inline long syscall7(long num, long a0, long a1, long a2, long a3,
                             long a4, long a5, long a6) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x3 asm("x3") = a3;
    register long x4 asm("x4") = a4;
    register long x5 asm("x5") = a5;
    register long x6 asm("x6") = a6;
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x6), "r"(x16)
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
    register long r0 asm("x0") = 1;
    register long r1 asm("x1") = (long)s;
    register long r2 asm("x2") = (long)n;
    register long r16 asm("x16") = SYS_write;
    asm volatile("svc #0x80" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r16) : "memory", "cc");
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
    register long r0 asm("x0") = code;
    register long r16 asm("x16") = SYS_exit;
    asm volatile("svc #0x80" : "+r"(r0) : "r"(r16) : "memory", "cc");
    for (;;) { }
}

static volatile long flag;

/*
 * wq_entry - the real wqthread entry point, reached via real
 * workq_setup_thread()'s thread_set_wq_state64()+thread_bootstrap_return()
 * (see this file's header comment). Raw asm, no C ABI assumed on entry
 * (real arm64 wqthread convention, not the plain AAPCS64 a C caller would
 * set up) - computes &flag itself via PC-relative addressing since x0 is
 * "self" (pthread_t), not a caller-supplied argument.
 */
__attribute__((naked)) static void wq_entry(void) {
    asm volatile(
        "adrp x9, _flag@PAGE\n"
        "add  x9, x9, _flag@PAGEOFF\n"
        "mov  x1, #0xabcd\n"
        "movk x1, #0x1234, lsl #16\n"
        "str  x1, [x9]\n"
        "1: b 1b\n"
    );
}

#define MAGIC 0x1234abcdUL
#define SPIN_LIMIT 400000000UL

__attribute__((used)) static void main_test(void) {
    long r;
    unsigned long i;

    put("workq_kthread_test: starting\n");
    flag = 0;

    /* Step 1: real bsdthread_register(2) - registers wq_entry as this
     * process's real wqthread entry point. */
    r = syscall7(SYS_bsdthread_register,
                 0 /* threadstart, unused by this test */,
                 (long)(unsigned long)wq_entry /* wqthread */,
                 0 /* flags -> real_bsdthread_register's pthsize slot */,
                 0 /* stack_addr_hint -> pthread_init_data; 0 = non-blob path */,
                 0 /* targetconc_ptr -> pthread_init_data_size */,
                 0 /* dispatchqueue_offset */,
                 0 /* tsd_offset */);
    if (r < 0) {
        put("bsdthread_register FAILED, errno=");
        put_dec(-r);
        put("\n");
        sys_exit(1);
    }
    put("bsdthread_register OK\n");

    /* Step 2: real workq_open(2) - allocates this process's workqueue. */
    r = syscall0(SYS_workq_open);
    if (r < 0) {
        put("workq_open FAILED, errno=");
        put_dec(-r);
        put("\n");
        sys_exit(1);
    }
    put("workq_open OK\n");

    /* Step 3: real workq_kernreturn(2) WQOPS_QUEUE_REQTHREADS - requests
     * one real worker thread at a real QOS_LEGACY-equivalent priority
     * (pp=0x8FF, see this file's header comment for the derivation). */
    r = syscall4(SYS_workq_kernreturn, WQOPS_QUEUE_REQTHREADS, 0, 1, 0x8FF);
    if (r < 0) {
        put("workq_kernreturn(REQTHREADS) FAILED, errno=");
        put_dec(-r);
        put("\n");
        sys_exit(1);
    }
    put("workq_kernreturn(REQTHREADS) OK, waiting for real workqueue thread...\n");

    for (i = 0; i < SPIN_LIMIT; i++) {
        if (flag == (long)MAGIC) {
            put("SUCCESS: real workqueue thread ran via real workq_setup_thread "
                "and wrote the magic value\n");
            sys_exit(0);
        }
    }

    put("TIMEOUT: workqueue thread never observed to run\n");
    sys_exit(1);
}

/* ------------------------------- entry point -------------------------------
 * Same pattern as tools/init_binary/init.c and
 * tools/pthread_kthread_test/pthread_kthread_test.c: never trust the
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
