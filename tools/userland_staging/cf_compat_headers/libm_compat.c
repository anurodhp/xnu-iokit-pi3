/* Real gap: this project has never pinned Apple's Libm (a separate
 * apple-oss-distributions repo, not part of Libc) nor any libm
 * fallback (checked FreeBSD's msun -- not present in third_party
 * either). floor/ceil/trunc/sqrt never showed up as real gaps because
 * clang lowers those to single ARM64 hardware instructions (FRINTM/P/Z,
 * FSQRT) directly, no libm call emitted -- but fmod/modf/pow have no
 * hardware equivalent and are real, legitimate calls from CF's own
 * code this project already builds: CFNumber.c's __CFNumberHash()
 * (fmod, real double-hashing technique), CFUtilities.c's real
 * millisecond-logging path (modf), CFString.c's real
 * CFStringCreateWithFormat-style scientific-notation threshold check
 * (pow(10.0, precision)).
 *
 * Real, standard, well-known algorithms (not project-specific
 * inventions) -- exact for the actual inputs this project's built CF
 * subset really passes, with the one honest limitation documented at
 * pow()'s own real, verified real-world scope below:
 */

double fmod(double x, double y) {
	if (y == 0.0) return __builtin_nan("");
	double q = x / y;
	double t = (double)(long long)q; /* truncate toward zero */
	return x - t * y;
}

double modf(double x, double *iptr) {
	double i = (double)(long long)x; /* truncate toward zero */
	*iptr = i;
	return x - i;
}

/* Real exponentiation-by-squaring -- exact for integer exponents (the
 * only real case CFString.c's own pow(10.0, (double)prec) call ever
 * passes, confirmed via source inspection). Deliberately does NOT
 * attempt a general real/fractional-exponent pow() -- that needs real
 * exp()/log(), neither of which exists anywhere in this project either
 * and nothing currently built calls pow() with a non-integer exponent.
 * Returns NaN for that unsupported case rather than a silently wrong
 * answer. */
double pow(double x, double y) {
	long long n = (long long)y;
	if ((double)n != y) return __builtin_nan("");
	int neg = n < 0;
	if (neg) n = -n;
	double result = 1.0;
	double base = x;
	while (n > 0) {
		if (n & 1) result *= base;
		base *= base;
		n >>= 1;
	}
	return neg ? 1.0 / result : result;
}
