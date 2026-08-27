/* Real, tiny standard C functions libxml2 calls that aren't in this
 * project's curated libc archives yet -- see build_libxml2.sh's own
 * header comment for the real trial-link finding (checked via nm
 * against every existing archive, not assumed). labs/rand_r are
 * genuinely trivial, standard, and carry zero behavioral risk, unlike
 * the other real gaps this trial found (_time/_sched_yield, real
 * syscall wrappers, deliberately deferred instead -- see
 * build_libxml2.sh).
 */
long labs(long x) {
    return x < 0 ? -x : x;
}

/* Real POSIX reentrant PRNG algorithm (a standard 48-bit linear
 * congruential generator, the same one glibc/BSD rand_r
 * implementations use) -- not cryptographic, matches libxml2's own
 * actual use (schema/pattern-matching internal tie-breaking, never
 * security-sensitive). */
int rand_r(unsigned int *seed) {
    unsigned long next = *seed;
    long result;

    next *= 1103515245;
    next += 12345;
    result = (unsigned int)(next / 65536) % 2048;

    next *= 1103515245;
    next += 12345;
    result <<= 10;
    result ^= (unsigned int)(next / 65536) % 1024;

    next *= 1103515245;
    next += 12345;
    result <<= 10;
    result ^= (unsigned int)(next / 65536) % 1024;

    *seed = (unsigned int)next;
    return (int)result;
}

/* Real log10(), which this project has never had anywhere (no Libm,
 * no libm fallback -- see cf_compat_headers/libm_compat.c's own header
 * for the same real gap and CF's real fmod/pow needs, reused
 * unmodified below since libxml2's own real call sites -- xpath.c's
 * `fmod(arg1, arg2)` for XPath's `mod` operator and
 * `pow(10.0, (double)exponent)` for scientific-notation parsing, both
 * confirmed via source inspection -- match the exact general-double
 * and integer-exponent cases that file's own fmod/pow already handle
 * correctly). xpath.c's one real log10() call
 * (`integer_place = (int)log10(absolute_value)`) only needs the
 * integer digit-count of a number for XPath's number-to-string
 * formatting -- a real but low-precision-tolerant use (worst case on a
 * wrong answer is a cosmetic off-by-one in digit grouping right at a
 * power-of-10 boundary, not a wrong computed value or a crash), so a
 * real, standard, but approximate algorithm is proportionate here
 * rather than a fully precise one: extract the real IEEE 754 base-2
 * exponent directly from the double's bit pattern (equivalent to
 * frexp(), which this project also doesn't have), then a real fast-
 * converging series (the standard atanh-based ln((1+z)/(1-z)) identity
 * -- z = (m-1)/(m+1) for mantissa m in [1,2), converges far faster
 * than a plain Taylor series over that whole range) for the mantissa's
 * own natural log, converted to base 10. */
#include <stdint.h>
double log10(double x) {
    if (x < 0.0) return __builtin_nan("");
    if (x == 0.0) return -__builtin_huge_val();

    /* Real bug found+fixed while writing this: brace-initializing a
     * union (`= { value }`) always initializes its FIRST member, not
     * whichever member matches value's type -- `mu = { mantissa_bits }`
     * silently did a numeric double-from-uint64_t conversion instead of
     * the intended bit reinterpretation, confirmed with a standalone
     * host-side test comparing against real libm log10() before this
     * was ever wired into the real build. Assigning `.bits` explicitly
     * (not via brace-init) avoids that trap in both directions. */
    union { double d; uint64_t bits; } u;
    u.d = x;
    int exp2 = (int)((u.bits >> 52) & 0x7FF) - 1023;
    uint64_t mantissa_bits = (u.bits & 0x000FFFFFFFFFFFFFULL) | 0x3FF0000000000000ULL;
    union { double d; uint64_t bits; } mu;
    mu.bits = mantissa_bits;
    double m = mu.d; /* m is in [1.0, 2.0) */

    double z = (m - 1.0) / (m + 1.0);
    double z2 = z * z;
    double term = z;
    double sum = term;
    for (int i = 1; i < 8; i++) {
        term *= z2;
        sum += term / (double)(2 * i + 1);
    }
    double ln_m = 2.0 * sum;

    const double ln2 = 0.6931471805599453;
    const double ln10 = 2.302585092994046;
    return ((double)exp2 * ln2 + ln_m) / ln10;
}
