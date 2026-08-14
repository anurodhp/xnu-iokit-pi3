/* Real smoketest for build_libplatform.sh's libplatform.a - exercises the
 * real os_unfair_lock fast path (lock/unlock, uncontended - this target
 * is provably single-threaded, see compat_stubs.c/tsd_bootstrap.c), real
 * os_once (one-time-init gate), and real OSAtomicIncrement32Barrier/
 * OSAtomicDecrement32Barrier - all genuine Apple libplatform code, not
 * the compat_stubs.c no-ops this build retires.
 */

#include <os/lock.h>
#include <os/lock_private.h>
#include <os/once_private.h>
#include <libkern/OSAtomic.h>

typedef unsigned long size_t;

/* Deliberately the base TSD bootstrap, not pthread_tsd_shim_bootstrap_for_libc()
 * (pthread_tsd_shim_libc_bootstrap.c) - that wrapper also calls Libc's
 * __xlocale_init(), which would make this smoketest depend on
 * libc_locale.a, which itself now depends on libplatform.a (real
 * os_unfair_lock/OSAtomic - see build_libplatform.sh). This smoketest
 * only needs a live TPIDRRO_EL0, not locale. */
extern int pthread_tsd_shim_bootstrap(void);
extern int libplatform_thread_self_bootstrap(void);

/* Same raw-syscall pattern as libc_locale_smoketest.c/libc_string_smoketest.c
 * (SYS_write=4, SYS_exit=1) rather than linking against the generated
 * libsyscall stubs (build_libsyscall_smoketest.sh's own separate output,
 * not part of this build's dependency chain). */
static long syscall3(long num, long a0, long a1, long a2) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x16 asm("x16") = num;
    asm volatile("svc #0x80" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x16) : "memory", "cc");
    return x0;
}

static void put(const char *s) {
    const char *p = s;
    while (*p) {
        p++;
    }
    syscall3(4 /* SYS_write */, 1, (long)s, (long)(p - s));
}

/* Real symbol Csu's crt.c calls after main() returns (see
 * libc_locale_smoketest.c's identical `void exit(int code)` - same
 * pattern, not a guess). */
void exit(int code) __attribute__((noreturn));
void exit(int code) {
    syscall3(1 /* SYS_exit */, code, 0, 0);
    __builtin_unreachable();
}
#define _exit exit

static os_unfair_lock g_lock = OS_UNFAIR_LOCK_INIT;
static int g_protected_value = 0;

static os_once_t g_once_token;
static int g_once_ran = 0;

static void once_init(void *ctxt) {
    (void)ctxt;
    g_once_ran++;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (!pthread_tsd_shim_bootstrap()) {
        put("FAIL: pthread_tsd_shim_bootstrap\n");
        _exit(1);
    }
    if (!libplatform_thread_self_bootstrap()) {
        put("FAIL: libplatform_thread_self_bootstrap\n");
        _exit(1);
    }

    /* Real os_unfair_lock: lock, mutate, unlock. A prior implementation
     * bug here (unpopulated TSD owner slot) would make this either hang
     * in the real contention slow path or silently corrupt lock state -
     * see build_libplatform.sh's header comment. */
    os_unfair_lock_lock(&g_lock);
    g_protected_value = 42;
    os_unfair_lock_unlock(&g_lock);
    if (g_protected_value != 42) {
        put("FAIL: os_unfair_lock round-trip\n");
        _exit(1);
    }
    put("PASS: real os_unfair_lock lock/unlock\n");

    /* Real os_once: init callout must run exactly once. */
    /* Call _os_once() directly rather than the once_private.h header's
     * os_once() wrapper: that wrapper is declared __header_always_inline
     * with C99 "extern inline" linkage semantics (no static, no
     * force-emit) - if clang ever doesn't fully inline it (observed at
     * -O0 in this build), it leaves a real external reference to a
     * plain, non-underscore-prefixed "os_once" symbol that nothing here
     * defines, since real Apple builds provide that inline expansion at
     * every call site by construction, not as a separately linkable
     * symbol. _os_once() is the real always-out-of-line API this
     * archive already exports and lock.c's real algorithm is entirely
     * implemented in - equally real, no wrapper-linkage risk. */
    _os_once(&g_once_token, 0, once_init);
    _os_once(&g_once_token, 0, once_init);
    if (g_once_ran != 1) {
        put("FAIL: os_once ran more than once\n");
        _exit(1);
    }
    put("PASS: real os_once runs exactly once\n");

    /* Real OSAtomic*Barrier. */
    volatile int counter = 10;
    int r1 = OSAtomicIncrement32Barrier((volatile int32_t *)&counter);
    int r2 = OSAtomicDecrement32Barrier((volatile int32_t *)&counter);
    if (r1 != 11 || r2 != 10 || counter != 10) {
        put("FAIL: OSAtomic round-trip\n");
        _exit(1);
    }
    put("PASS: real OSAtomicIncrement32Barrier/OSAtomicDecrement32Barrier\n");

    put("ALL PASS: libplatform smoketest\n");
    _exit(0);
}
