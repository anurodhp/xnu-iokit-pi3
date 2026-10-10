/* Host test for iokit/BCM2837PWM/BCM2837PWMMath.h (DARW-314).
 * cc -I iokit/BCM2837PWM -o /tmp/pwm_math_test tools/tests/pwm_math_test.c && /tmp/pwm_math_test */
#include <assert.h>
#include <stdio.h>
#include "BCM2837PWMMath.h"

int main(void)
{
	assert(kPWMClockHz == 250000000U);
	struct { uint32_t rate, rng; } want[] = { { 44100, 5669 }, { 48000, 5208 }, { 32000, 7813 }, { 96000, 2604 } };
	for (int i = 0; i < 4; i++) {
		uint32_t rng = pwmRangeFor(pwmTargetClockHz(want[i].rate), want[i].rate);
		uint32_t actual = pwmActualRateHz(kPWMClockHz, rng);
		int64_t err = (int64_t)actual - want[i].rate;
		if (err < 0) err = -err;
		printf("rate %u: range %u actual %u err %lld Hz\n", want[i].rate, rng, actual, (long long)err);
		assert(rng == want[i].rng);
		assert(err * 1000 <= want[i].rate);          // within 0.1%
	}
	assert(pwmRangeFor(kPWMClockHz, 8000) == 0);     // range 31250 > max
	assert(pwmRangeFor(kPWMClockHz, 30000) == 0);
	assert(pwmRangeFor(1, 0) == 0);
	assert(pwmDatFromS16(-32768, 2083) == 0);
	assert(pwmDatFromS16(0, 2083) == 1041);
	assert(pwmDatFromS16(32767, 2083) == 2082);
	for (int r = 1024; r <= 8192; r += 7) {
		for (int s = -32768; s <= 32767; s += 13) {
			assert(pwmDatFromS16((int16_t)s, (uint32_t)r) < (uint32_t)r);
		}
	}
	puts("pwm_math_test: PASS");
	return 0;
}
