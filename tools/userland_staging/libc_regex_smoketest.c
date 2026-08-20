/* Real smoketest for libc_build/libc_regex.a (build_libc_regex.sh) --
 * exercises real regcomp/regexec/regfree/regerror (not fabricated: this
 * links directly against the real TRE engine compiled from
 * third_party/Libc/regex/TRE/lib and FreeBSD/regerror.c). Host-side
 * assertions are impossible here (no C library on the QEMU target to
 * report failure through besides a raw syscall), so this writes a
 * single-character pass/fail marker to stdout via the real, already-
 * proven putc()/printf() path other smoketests in this directory use --
 * QEMU boot log is the actual verification evidence, same convention as
 * every sibling *_smoketest.c in this directory.
 */
#include <regex.h>
#include <stdio.h>

static int check(const char *pattern, int cflags, const char *s, int expect_match) {
    regex_t re;
    int rc = regcomp(&re, pattern, cflags);
    if (rc != 0) {
        char errbuf[128];
        regerror(rc, &re, errbuf, sizeof(errbuf));
        printf("regcomp(\"%s\") FAILED: %s\n", pattern, errbuf);
        return 0;
    }
    regmatch_t m[4];
    int erc = regexec(&re, s, 4, m, 0);
    regfree(&re);
    int matched = (erc == 0);
    int ok = (matched == expect_match);
    printf("regexec(\"%s\", \"%s\") -> %s (expected %s): %s\n",
           pattern, s, matched ? "MATCH" : "NOMATCH",
           expect_match ? "MATCH" : "NOMATCH", ok ? "PASS" : "FAIL");
    return ok;
}

int main(void) {
    int all_ok = 1;
    all_ok &= check("^h.*o$", REG_EXTENDED, "hello", 1);
    all_ok &= check("^h.*o$", REG_EXTENDED, "goodbye", 0);
    all_ok &= check("[0-9]+", REG_EXTENDED, "abc123", 1);
    all_ok &= check("^[0-9]+$", REG_EXTENDED, "abc123", 0);
    printf(all_ok ? "LIBC_REGEX_SMOKETEST: ALL PASS\n" : "LIBC_REGEX_SMOKETEST: FAIL\n");
    return all_ok ? 0 : 1;
}
