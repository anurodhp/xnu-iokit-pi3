/* Real TPIDRRO_EL0 TSD-base install, same real mechanism as
 * tsd_bootstrap.c/pthread_tsd_shim.c (platform_syscall "set/get cthread
 * self", QEMU-confirmed 2026-08-13) - re-extracted standalone here
 * because those two files ALSO define pthread_key_create/
 * pthread_setspecific/pthread_getspecific/pthread_once as their own
 * simplified stand-ins, which now real, genuine
 * third_party/libpthread/src/pthread_tsd.c provides instead (this
 * project's real libpthread tier) - linking both would be a real
 * duplicate-symbol conflict, not a hypothetical one. Only the low-level
 * bootstrap primitive is needed by real libpthread; the higher-level
 * shim functions are superseded, not both kept.
 *
 * Real difference from tsd_bootstrap.c: that file bootstraps a
 * STANDALONE synthetic TSD block. This one bootstraps &_main_thread.tsd[0]
 * (the real main-thread pthread_s's own embedded tsd[] field) - see
 * pthread_main_thread_bootstrap.c's own header comment for why that
 * specific base pointer is required by real _pthread_self_direct()'s
 * arm64 offset arithmetic.
 */

#define PLATFORM_SYSCALL_TRAP_NO 0x80000000

static void platform_syscall_set_cthread_self(void *tsd_base) {
    register long x0 asm("x0") = (long)tsd_base;
    register long x3 asm("x3") = 2; /* set cthread self */
    register long x16 asm("x16") = PLATFORM_SYSCALL_TRAP_NO;
    asm volatile("svc #0x80" : "+r"(x0) : "r"(x3), "r"(x16) : "memory", "cc", "x1", "x2");
}

static void *platform_syscall_get_cthread_self(void) {
    register long x0 asm("x0") = 0;
    register long x3 asm("x3") = 3; /* get cthread self */
    register long x16 asm("x16") = PLATFORM_SYSCALL_TRAP_NO;
    asm volatile("svc #0x80" : "+r"(x0) : "r"(x3), "r"(x16) : "memory", "cc", "x1", "x2");
    return (void *)x0;
}

static int g_tsd_bootstrapped = 0;

int pthread_tsd_shim_bootstrap_with_base(void *tsd_base) {
    if (g_tsd_bootstrapped) {
        return 1;
    }
    platform_syscall_set_cthread_self(tsd_base);
    void *readback = platform_syscall_get_cthread_self();
    g_tsd_bootstrapped = (readback == tsd_base);
    return g_tsd_bootstrapped;
}
