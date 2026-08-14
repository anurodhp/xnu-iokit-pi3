/* Smoketest: exercises the newly-unblocked (real TSD) Libc locale/ctype
 * functions. strcasecmp()/strncasecmp() are real Apple Libc source
 * (third_party/Libc/string/FreeBSD/strcasecmp.c) that internally goes
 * through __current_locale() -> real TSD (against ___locale_key, see
 * xlocale_private.h) and real per-locale ctype tables (tolower_l).
 *
 * MUST call pthread_tsd_shim_bootstrap_for_libc() first (see that
 * function's own comment in pthread_tsd_shim_libc_bootstrap.c) - an
 * earlier version of this file didn't, and an earlier version of its own
 * comment claimed this "exercises the full real chain", which an
 * adversarial review correctly called out as not actually true without
 * the eager bootstrap: real Libc's fast TSD-read path
 * (_pthread_getspecific_direct -> raw TPIDRRO_EL0 read) bypasses this
 * project's shim functions entirely, and __locale_key was never
 * initialized without __xlocale_init() having run. Both are true now
 * that this file calls the combined bootstrap explicitly - "verified"
 * below means "linked and, once boot-tested under QEMU, ran" - compiling
 * and linking cleanly on the host is necessary but not sufficient proof
 * by itself (see docs/roadmap.md Phase 4 for what boot-testing has and
 * hasn't confirmed for this specific smoketest).
 */
typedef unsigned long size_t;

extern int pthread_tsd_shim_bootstrap_for_libc(void);
extern int strcasecmp(const char *, const char *);
extern int strncasecmp(const char *, const char *, size_t);

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
    syscall3(4 /* SYS_write */, 1, (long)s, (long)n);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    if (!pthread_tsd_shim_bootstrap_for_libc()) {
        put("libc_locale_smoketest: FAILED (bootstrap)\n");
        return 1;
    }

    int ok = 1;
    /* Real, locale-aware, case-insensitive comparison - exercises the
     * full real chain: __current_locale() -> TSD (pthread_getspecific)
     * -> tolower_l() against the real "C" locale's ctype table. */
    ok &= (strcasecmp("Hello", "HELLO") == 0);
    ok &= (strcasecmp("Hello", "World") != 0);
    ok &= (strncasecmp("HELLOworld", "hello_XYZ", 5) == 0);
    ok &= (strncasecmp("HELLOworld", "hello_XYZ", 6) != 0);

    if (ok) {
        put("libc_locale_smoketest: real strcasecmp/strncasecmp work through real TSD+locale\n");
    } else {
        put("libc_locale_smoketest: FAILED\n");
        return 1;
    }
    return 0;
}

void exit(int code) {
    syscall3(1 /* SYS_exit */, code, 0, 0);
    for (;;) { }
}
