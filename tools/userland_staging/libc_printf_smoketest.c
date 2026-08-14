/* Real Apple/FreeBSD printf/scanf smoketest: proves the xprintf tier
 * (build_libc_printf.sh) actually formats/parses real data end to end,
 * not just that the archive links. Uses real printf (stdout, via the real
 * __sF table from libc_stdio.a) plus real sprintf/snprintf/sscanf against
 * an in-memory buffer - covers %d/%s/%f/%x conversions and a real
 * multi-field sscanf parse.
 *
 * MUST call pthread_tsd_shim_bootstrap_for_libc() first - see that
 * function's own comment in pthread_tsd_shim_libc_bootstrap.c. Every real
 * printf/scanf-family function goes through __current_locale() (snprintf.c
 * itself calls it directly), which reads a TSD slot that's flat-out
 * uninitialized without this call - a real, reproducible SIGSEGV inside
 * snprintf() on first QEMU boot-test confirmed this the hard way (root-
 * caused via a raw-syscall-only checkpoint bisection, since this crash
 * happens before any of this tier's own machinery - including printf
 * itself - can be trusted to report anything), not guessed from reading
 * the other smoketests' comments alone.
 */

#include <stdio.h>
#include <string.h>

extern int pthread_tsd_shim_bootstrap_for_libc(void);

int main(void) {
    if (!pthread_tsd_shim_bootstrap_for_libc()) {
        return 2;
    }

    char buf[128];
    int n = snprintf(buf, sizeof(buf), "%d %s %.2f 0x%x", 42, "hello", 3.5, 255);
    if (n <= 0) {
        printf("FAIL: snprintf returned %d\n", n);
        return 1;
    }
    if (strcmp(buf, "42 hello 3.50 0xff") != 0) {
        printf("FAIL: snprintf produced wrong output: '%s'\n", buf);
        return 1;
    }

    int ival = 0;
    char sval[32];
    double fval = 0.0;
    int matched = sscanf(buf, "%d %31s %lf", &ival, sval, &fval);
    if (matched != 3) {
        printf("FAIL: sscanf matched %d fields, expected 3\n", matched);
        return 1;
    }
    if (ival != 42 || strcmp(sval, "hello") != 0 || fval < 3.49 || fval > 3.51) {
        printf("FAIL: sscanf parsed wrong values: ival=%d sval='%s' fval=%f\n", ival, sval, fval);
        return 1;
    }

    printf("PASS: real snprintf/sscanf round-trip (%%d/%%s/%%f/%%x): '%s' -> %d,%s,%.2f\n",
           buf, ival, sval, fval);
    return 0;
}
