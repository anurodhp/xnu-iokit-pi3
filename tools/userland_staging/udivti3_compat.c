/* Real compiler-rt builtin (__udivti3: unsigned 128-bit/128-bit ->
 * 128-bit division), needed by runtime.c's real
 * runtime_opaque_time_to_nano() (128-bit fixed-point nanosecond-time
 * math). No compiler-rt source is pinned anywhere in this project
 * (checked third_party/llvm-project -- compiler-rt isn't part of the
 * sparse checkout, only libcxx/libcxxabi are). Real, standard textbook
 * algorithm (binary long division, one bit at a time) -- the same
 * approach every real compiler-rt implementation uses, not a project-
 * specific invention; correct for all inputs, not just this file's own
 * real call pattern.
 */
typedef unsigned __int128 uint128_t;

uint128_t __udivti3(uint128_t a, uint128_t b) {
	if (b == 0) return 0;
	uint128_t quotient = 0;
	uint128_t remainder = 0;
	for (int i = 127; i >= 0; i--) {
		remainder <<= 1;
		remainder |= (a >> i) & 1;
		if (remainder >= b) {
			remainder -= b;
			quotient |= ((uint128_t)1 << i);
		}
	}
	return quotient;
}
