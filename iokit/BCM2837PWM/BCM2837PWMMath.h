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
// high to bother; 8192 (13 bits) leaves room for the nominal PLLD/2 setting.
#define kPWMMinRange 1024U
#define kPWMMaxRange 8192U
// PWM clock: PLLD (500 MHz nominal on the BCM2837) divided by 2, an integer
// divide so there is no fractional-divider (MASH) jitter; the same choice as
// Circle (lib/sound/pwmsoundbasedevice.cpp, CLOCK_RATE 250000000 for Pi 1-3,
// with machineinfo.cpp's PLLD = 500 MHz). Nominal frame rate 250 MHz / RANGE
// (5208 at 48 kHz). On a real Pi 3 the PWM actually runs at 5/8 of the nominal
// rate (measured, DARW-317), so BCM2837PWMAudioEngine::calibrateRate() trims
// RANGE at start; with /2 that lands near 3255 at 48 kHz, about 11.7 bits.
#define kPWMPllDHz       500000000U
#define kPWMClockDivisor 2U
#define kPWMClockHz      (kPWMPllDHz / kPWMClockDivisor)

// Nominal PWM clock (the same for every rate; the engine trims the range).
static inline uint32_t
pwmTargetClockHz(uint32_t rateHz)
{
	(void)rateHz;
	return kPWMClockHz;
}

// Range for a clock rate, rounded to nearest.
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
