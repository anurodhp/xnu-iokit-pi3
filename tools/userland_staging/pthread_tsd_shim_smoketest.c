/* Proves pthread_tsd_shim.c's real-named (pthread_key_create/setspecific/
 * getspecific) functions work standalone, independent of the deeper Libc
 * locale/stdio/regex chain (see docs/roadmap.md Phase 4 for why that
 * chain hits a separate, later wall - this smoketest isolates the part
 * that's fully resolved and verified: the shim itself).
 *
 * Calls pthread_tsd_shim_bootstrap() explicitly first - per the real bug
 * an adversarial review found (see pthread_tsd_shim.c's own comment on
 * pthread_tsd_shim_bootstrap()): the bootstrap is NOT optional/lazy, real
 * Libc's fast TSD-read path bypasses this shim's functions entirely and
 * needs TPIDRRO_EL0 already live.
 */

extern int pthread_tsd_shim_bootstrap(void);
extern int pthread_key_create(unsigned long *key, void (*destructor)(void *));
extern int pthread_setspecific(unsigned long key, const void *value);
extern void *pthread_getspecific(unsigned long key);
extern int _pthread_key_create(unsigned long *key, void (*destructor)(void *));
extern int _pthread_setspecific(unsigned long key, const void *value);
extern void *_pthread_getspecific(unsigned long key);

typedef unsigned long size_t;

static long syscall3(long num, long a0, long a1, long a2) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x16 asm("x16") = num;
    asm volatile("svc #0x80" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x16) : "memory", "cc");
    return x0;
}

static void put(const char *s) {
    size_t n = 0;
    while (s[n]) {
        n++;
    }
    syscall3(4, 1, (long)s, (long)n);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    if (!pthread_tsd_shim_bootstrap()) {
        put("pthread_tsd_shim_smoketest: FAILED (bootstrap)\n");
        return 1;
    }

    int ok = 1;
    unsigned long k1 = (unsigned long)-1, k2 = (unsigned long)-1;
    ok &= (pthread_key_create(&k1, 0) == 0);
    ok &= (pthread_key_create(&k2, 0) == 0);
    ok &= (k1 != k2);
    ok &= (pthread_setspecific(k1, (void *)0x1111UL) == 0);
    ok &= (pthread_getspecific(k1) == (void *)0x1111UL);
    ok &= (pthread_getspecific(k2) == 0);

    /* Real Apple private aliases (reentrant.h's thr_keycreate/etc) */
    unsigned long k3 = (unsigned long)-1;
    ok &= (_pthread_key_create(&k3, 0) == 0);
    ok &= (k3 != k1 && k3 != k2);
    ok &= (_pthread_setspecific(k3, (void *)0x2222UL) == 0);
    ok &= (_pthread_getspecific(k3) == (void *)0x2222UL);
    /* confirm k1's value is untouched by k3's write - real independent slots */
    ok &= (pthread_getspecific(k1) == (void *)0x1111UL);

    if (ok) {
        put("pthread_tsd_shim_smoketest: real pthread_key_create/setspecific/getspecific "
            "(public + private-alias names) all work\n");
    } else {
        put("pthread_tsd_shim_smoketest: FAILED\n");
        return 1;
    }
    return 0;
}

void exit(int code) {
    syscall3(1, code, 0, 0);
    for (;;) { }
}
