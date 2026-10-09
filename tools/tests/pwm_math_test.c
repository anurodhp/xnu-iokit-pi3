/* Host test for iokit/BCM2837PWM/BCM2837PWMMath.h (DARW-314).
 * cc -I iokit/BCM2837PWM -o /tmp/pwm_math_test tools/tests/pwm_math_test.c && /tmp/pwm_math_test */
#include <assert.h>
#include <stdio.h>
#include "BCM2837PWMMath.h"

int main(void)
{
	uint32_t rates[] = { 44100, 48000 };
	for (int i = 0; i < 2; i++) {
		uint32_t want = pwmTargetClockHz(rates[i]);
		assert(pwmRangeFor(want, rates[i]) == kPWMTargetRange);
		// A firmware clock off by up to ~0.5% still gives a range within one
		// step, and an actual rate within 0.1% of the requested one.
		uint32_t got = want - want / 200;
		uint32_t rng = pwmRangeFor(got, rates[i]);
		assert(rng != 0);
		uint32_t actual = pwmActualRateHz(got, rng);
		int64_t err = (int64_t)actual - rates[i];
		if (err < 0) err = -err;
		printf("rate %u: clock %u rng %u actual %u err %lld Hz\n", rates[i], got, rng, actual, (long long)err);
		assert(err * 1000 <= rates[i]);
	}
	assert(pwmRangeFor(1000000, 48000) == 0);       // clock far too slow
	assert(pwmRangeFor(1000000000U, 48000) == 0);   // far too fast
	assert(pwmRangeFor(1, 0) == 0);
	assert(pwmDatFromS16(-32768, 2048) == 0);
	assert(pwmDatFromS16(0, 2048) == 1024);
	assert(pwmDatFromS16(32767, 2048) == 2047);
	for (int s = -32768; s <= 32767; s++) {
		assert(pwmDatFromS16((int16_t)s, 2083) < 2083);
	}
	puts("pwm_math_test: PASS");
	return 0;
}
