/* Smoketest: proves the newly-unblocked (real TSD) Libc locale/ctype
 * functions actually work, not just link. strcasecmp()/strncasecmp() are
 * real Apple Libc source (third_party/Libc/string/FreeBSD/strcasecmp.c)
 * that internally goes through __current_locale() -> real TSD
 * (pthread_getspecific against ___locale_key, see xlocale_private.h) and
 * real per-locale ctype tables (tolower_l), all gated on TSD until this
 * session's bootstrap work (see docs/roadmap.md Phase 4).
 */
typedef unsigned long size_t;

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
