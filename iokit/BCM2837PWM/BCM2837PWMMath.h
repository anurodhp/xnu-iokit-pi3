/*
 * Pure arithmetic for the PWM audio path (DARW-314), kept free of kernel
 * headers so tools/tests/pwm_math_test.c can check it on the host.
 *
 * Model: both PWM channels run the same range RNG on one clock. In FIFO mode
 * each channel takes one FIFO word per period of RNG clocks, so the stereo
 * sample rate is clock / RNG and a sample is a DAT value 0..RNG.
 */
#ifndef BCM2837PWMMATH_H
#define BCM2837PWMMATH_H

#include <stdint.h>

// Range limits: below 1024 steps (10 bits) the quantisation noise is too
// high to bother, above 4096 the clock gets needlessly fast.
#define kPWMMinRange 1024U
#define kPWMMaxRange 4096U
// Preferred range, 11 bits: 48000 * 2048 = 98.304 MHz.
#define kPWMTargetRange 2048U

static inline uint32_t
pwmTargetClockHz(uint32_t rateHz)
{
	return rateHz * kPWMTargetRange;
}

// Range for the clock the firmware actually gave us, rounded to nearest.
// Returns 0 if it falls outside [min, max].
static inline uint32_t
pwmRangeFor(uint32_t clockHz, uint32_t rateHz)
{
	if (rateHz == 0) {
		return 0;
	}
	uint32_t rng = (uint32_t)(((uint64_t)clockHz + rateHz / 2) / rateHz);
	return (rng < kPWMMinRange || rng > kPWMMaxRange) ? 0 : rng;
}

static inline uint32_t
pwmActualRateHz(uint32_t clockHz, uint32_t rng)
{
	return rng ? clockHz / rng : 0;
}

// Signed 16-bit sample -> DAT value in [0, rng). Mid-scale (0) is rng/2.
static inline uint32_t
pwmDatFromS16(int16_t s, uint32_t rng)
{
	return (uint32_t)(((uint64_t)((int32_t)s + 32768) * rng) >> 16);
}

#endif
