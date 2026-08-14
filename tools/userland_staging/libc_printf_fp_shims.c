/* Real IEEE754 double classification (__fpclassifyd) + real (payload-
 * simplified) NaN constructors (nan/nanf/nanl) - the last remaining real
 * dependencies pulled in by compiling gdtoa/_ldbl_util.c and
 * gdtoa/FreeBSD/gdtoa-hexnan.c (both needed for real hdtoa/hldtoa - long
 * double hex-float rendering, part of %a/%A conversions and NAN(tag)
 * literal scanning). These are genuinely libm functions in real Apple/
 * FreeBSD (third_party/Libm isn't cloned into this project at all - out
 * of scope per docs/roadmap.md Phase 4's step-1 library list, which
 * covers libSystem's dyld/Libc/libpthread/etc but not Libm), but each is
 * small/self-contained enough to write for real here rather than skip the
 * hex-float tier entirely - same "write custom only as last resort, but
 * for real" precedent as this project's own from-scratch SHA-256/384/512
 * (see architecture.md).
 *
 * __fpclassifyd: real bit-pattern inspection, no FP hardware instructions
 * needed - exact IEEE754 binary64 layout (1 sign / 11 exponent / 52
 * mantissa bits), verified against math.h's real FP_NAN/FP_INFINITE/
 * FP_ZERO/FP_NORMAL/FP_SUBNORMAL values above.
 *
 * nan()/nanf()/nanl(): real quiet-NaN-producing compiler builtins
 * (__builtin_nan family - genuine NaN values, not a fake stand-in), but
 * deliberately do NOT thread the caller's tag string through into the
 * NaN's mantissa payload bits the way real libm's nan() does (that needs
 * a real numeric-string parser this bootstrap point doesn't have reason
 * to duplicate) - honest, documented simplification: "some NaN" instead
 * of "the specific NaN this tag encodes". Only affects the payload bits
 * of a value whose whole point is being un-ordered/non-numeric; no
 * caller in this tier's real call graph inspects a NaN's payload.
 */

#include <stdint.h>

int __fpclassifyd(double d)
{
    union { double d; uint64_t u; } v;
    v.d = d;
    uint32_t exponent = (uint32_t)((v.u >> 52) & 0x7FF);
    uint64_t mantissa = v.u & 0xFFFFFFFFFFFFFULL;

    if (exponent == 0x7FF) {
        return mantissa == 0 ? 2 /* FP_INFINITE */ : 1 /* FP_NAN */;
    }
    if (exponent == 0) {
        return mantissa == 0 ? 3 /* FP_ZERO */ : 5 /* FP_SUBNORMAL */;
    }
    return 4; /* FP_NORMAL */
}

double nan(const char *tag __unused) { return __builtin_nan(""); }
float nanf(const char *tag __unused) { return __builtin_nanf(""); }
long double nanl(const char *tag __unused) { return __builtin_nanl(""); }
