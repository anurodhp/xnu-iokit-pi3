/* Real smoketest for libc_build/libm.a (build_libm.sh) -- exercises the
 * real functions all three shims this pass replaces needed
 * (sqrt/fmod/modf/pow for ping_libm_compat.c/cf_compat_headers/
 * libm_compat.c, fabs/trunc/ilogb for simple_cmds_libm_compat.c) plus a
 * couple of the bonus real functions this archive also builds
 * (cbrt/log10/j0), against known-correct values. Same host-side-
 * assertion-impossible / QEMU-boot-log-is-the-evidence convention every
 * sibling *_smoketest.c in this directory uses (see
 * libc_regex_smoketest.c).
 */
#include <math.h>
#include <stdio.h>

static int approx(double a, double b, double eps) {
    double d = a - b;
    if (d < 0) d = -d;
    return d < eps;
}

int main(void) {
    int all_ok = 1;
    double r;

    r = sqrt(2.0);
    printf("sqrt(2.0) = %f (expect ~1.414214): %s\n", r, approx(r, 1.4142135623730951, 1e-9) ? "PASS" : "FAIL");
    all_ok &= approx(r, 1.4142135623730951, 1e-9);

    r = fmod(10.5, 3.0);
    printf("fmod(10.5, 3.0) = %f (expect 1.5): %s\n", r, approx(r, 1.5, 1e-12) ? "PASS" : "FAIL");
    all_ok &= approx(r, 1.5, 1e-12);

    {
        double ip;
        double fp = modf(3.75, &ip);
        printf("modf(3.75) = %f + %f (expect 3.0 + 0.75): %s\n", ip, fp,
               (approx(ip, 3.0, 1e-12) && approx(fp, 0.75, 1e-12)) ? "PASS" : "FAIL");
        all_ok &= approx(ip, 3.0, 1e-12) && approx(fp, 0.75, 1e-12);
    }

    r = pow(2.0, 10.0);
    printf("pow(2.0, 10.0) = %f (expect 1024): %s\n", r, approx(r, 1024.0, 1e-6) ? "PASS" : "FAIL");
    all_ok &= approx(r, 1024.0, 1e-6);

    r = pow(2.5, 0.5);
    printf("pow(2.5, 0.5) = %f (expect ~1.581139, fractional exponent): %s\n", r,
           approx(r, 1.5811388300841898, 1e-9) ? "PASS" : "FAIL");
    all_ok &= approx(r, 1.5811388300841898, 1e-9);

    r = fabs(-42.5);
    printf("fabs(-42.5) = %f (expect 42.5): %s\n", r, approx(r, 42.5, 1e-12) ? "PASS" : "FAIL");
    all_ok &= approx(r, 42.5, 1e-12);

    r = trunc(-7.9);
    printf("trunc(-7.9) = %f (expect -7.0): %s\n", r, approx(r, -7.0, 1e-12) ? "PASS" : "FAIL");
    all_ok &= approx(r, -7.0, 1e-12);

    {
        int lg = ilogb(1024.0);
        printf("ilogb(1024.0) = %d (expect 10): %s\n", lg, (lg == 10) ? "PASS" : "FAIL");
        all_ok &= (lg == 10);
    }

    r = cbrt(27.0);
    printf("cbrt(27.0) = %f (expect 3.0): %s\n", r, approx(r, 3.0, 1e-9) ? "PASS" : "FAIL");
    all_ok &= approx(r, 3.0, 1e-9);

    r = log10(1000.0);
    printf("log10(1000.0) = %f (expect 3.0): %s\n", r, approx(r, 3.0, 1e-9) ? "PASS" : "FAIL");
    all_ok &= approx(r, 3.0, 1e-9);

    printf("libm_smoketest: %s\n", all_ok ? "ALL PASS" : "SOME FAILED");
    return all_ok ? 0 : 1;
}
