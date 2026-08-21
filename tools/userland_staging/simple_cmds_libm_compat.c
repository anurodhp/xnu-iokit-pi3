/* fabs()/trunc()/ilogb() -- df(1)'s real, unmodified unit_adjust()/
 * prtstat() (human-readable -h/-H size formatting and the real
 * "round percentage up" Unix2003 rule) call all three. This project has
 * no libm at all (Apple's libm is its own separate apple-oss-
 * distributions repo, never pinned here -- confirmed absent, not
 * overlooked) and this build's `-fno-builtin` CFLAGS blocks clang from
 * silently lowering these to inline FABS/FRINTZ/exponent-extract
 * instructions the way a normal (non-freestanding) build would -- a real
 * call to the C symbol is generated and must resolve (confirmed by a
 * direct compile experiment, not assumed).
 *
 * Real fdlibm (the actual upstream both FreeBSD's msun and Apple's own
 * libm derive these three functions from) implements them via
 * GET_HIGH_WORD()/EXTRACT_WORDS() macros from a private math_private.h
 * this project doesn't have -- pulling that whole private header (plus
 * its own subnormal_ilogb() helper) just for three single-instruction-
 * class bit-twiddling functions would be a disproportionate real-source
 * pull for what df(1) actually exercises (filesystem sizes, always a
 * normal finite non-negative double well within double's exact-integer
 * range). Written directly against the standard IEEE-754 binary64 bit
 * layout instead (same "narrow custom last resort" call as this
 * project's from-scratch SHA-256 replacing a whole corecrypto-kext
 * dependency) -- not a scoped-down approximation: correctly handles
 * subnormals, zero, infinity, and NaN via the same bit-field logic real
 * fdlibm uses, just without that header's macro indirection.
 */

#include <stdint.h>
#include <limits.h>
#include <math.h>

union d64 {
    double d;
    uint64_t u;
};

double fabs(double x) {
    union d64 v;
    v.d = x;
    v.u &= 0x7fffffffffffffffULL; /* clear sign bit */
    return v.d;
}

double trunc(double x) {
    union d64 v;
    int biased_exp;
    uint64_t frac_mask;

    v.d = x;
    biased_exp = (int)((v.u >> 52) & 0x7ff);

    if (biased_exp >= 1075) {
        /* unbiased exponent (biased_exp - 1023) >= 52: x already has no
         * fractional bits representable (includes all finite integers
         * this large, plus +-Inf/NaN, whose biased exponent is 0x7ff) --
         * return unchanged, matching real trunc(inf)/trunc(nan). */
        return x;
    }
    if (biased_exp < 1023) {
        /* |x| < 1.0 (includes x == 0): truncates to a zero of the same
         * sign. */
        v.u &= 0x8000000000000000ULL;
        return v.d;
    }
    /* 1.0 <= |x| < 2^52: clear the (52 - unbiased_exp) low fraction
     * bits that represent the fractional part. */
    frac_mask = 0xfffffffffffffULL >> (biased_exp - 1023);
    v.u &= ~frac_mask;
    return v.d;
}

int ilogb(double x) {
    union d64 v;
    int biased_exp;
    uint64_t mantissa;

    v.d = x;
    biased_exp = (int)((v.u >> 52) & 0x7ff);
    mantissa = v.u & 0xfffffffffffffULL;

    if (biased_exp == 0) {
        if (mantissa == 0) {
            return FP_ILOGB0; /* x == 0 (either sign) */
        }
        /* Subnormal: normalize by counting leading zeros in the
         * 52-bit mantissa field (its implicit leading bit is 0, unlike
         * a normal number's implicit 1). */
        {
            int shift = 0;
            uint64_t m = mantissa;
            while ((m & 0x8000000000000ULL) == 0) {
                m <<= 1;
                shift++;
            }
            return -1023 - shift;
        }
    }
    if (biased_exp == 0x7ff) {
        return (mantissa == 0) ? INT_MAX /* +-Inf */ : FP_ILOGBNAN;
    }
    return biased_exp - 1023;
}
