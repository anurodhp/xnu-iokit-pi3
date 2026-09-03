/*
 * SD clock divider math for the BCM2837's Arasan SDHCI controller
 * (DAR-103), deliberately split out of BCM2837SDHCI.cpp.
 *
 * WHY ITS OWN HEADER: this is the one piece of the driver where a subtly
 * wrong answer is worse than an outright failure. Programming a divisor
 * that yields an SD clock *faster* than intended does not produce a clean
 * error - it produces marginal timing, i.e. silently corrupted blocks
 * handed to the filesystem. It is also the one piece that needs no
 * hardware at all to evaluate, so it lives here as a pure, dependency-
 * free function that both the real driver and a host-side exhaustive test
 * (tools/test_sdhci_divider.sh) compile from this single source. A test
 * that re-implemented the algorithm alongside the driver's own copy would
 * prove nothing; this way the thing under test is literally the shipping
 * code.
 *
 * Algorithm: mirrors Linux's sdhci_calc_clk() (drivers/mmc/host/sdhci.c)
 * for the two non-programmable-clock cases - the same algorithm that runs
 * against this exact controller on a real Raspberry Pi via sdhci-iproc.
 *
 * Two divider encodings exist and they are NOT interchangeable. Writing
 * one controller's encoding to the other silently yields a completely
 * different (possibly much faster) clock, which is why the caller reads
 * the spec version out of HOST_VERSION rather than assuming it:
 *
 *   - SDHCI 1.00/2.00, "8-bit divided clock mode": SDCLK_FREQ_SEL is bits
 *     [15:8] of CLOCK_CONTROL and only a single bit may be set. Field
 *     value N (N != 0) means base/(2N); N == 0 means base/1. Achievable
 *     divisors: 1, 2, 4, ... 256.
 *   - SDHCI 3.00, "10-bit divided clock mode": the field grows to 10 bits
 *     - low 8 at [15:8], high 2 at [7:6] - and any value is legal. Field
 *     value N (N != 0) means base/(2N); N == 0 means base/1. Achievable
 *     divisors: 1, and every even number up to 2046.
 *
 * The function returns false rather than the closest-anyway (too fast)
 * divisor when nothing reaches the target. Every comparison is done as
 * `base <= target * div` in 64-bit rather than `base / div <= target`:
 * truncating division would accept a divisor whose true rate is fractions
 * of a hertz above target. That is harmless in practice and rejecting it
 * costs nothing, but "the guard is exact" is a much easier property to
 * re-verify later than "the guard is off by less than 1 Hz".
 */

#ifndef _BCM2837_SDHCI_DIVIDER_H
#define _BCM2837_SDHCI_DIVIDER_H

#include <stdint.h>
#if !defined(__cplusplus)
#include <stdbool.h>
#endif

/*
 * SDHCI_HOST_VERSION's spec-version field, same encoding/naming as
 * Linux's SDHCI_SPEC_100/200/300 (drivers/mmc/host/sdhci.h): the field
 * holds spec_version - 1.
 */
enum {
	kBCM2837SDHCISpecVer100 = 0,
	kBCM2837SDHCISpecVer200 = 1,
	kBCM2837SDHCISpecVer300 = 2,
};

/*
 * Produces the CLOCK_CONTROL divider field (bits already positioned, ready
 * to OR with the enable bits) for the fastest rate <= targetHz that this
 * controller's divider can express, plus that rate. Returns false - and
 * writes neither output - if there is no such divisor, if either frequency
 * is zero, or if the computed field does not fit its encoding.
 */
static inline bool
bcm2837SDHCICalcDivider(uint32_t baseHz, uint32_t targetHz,
    uint16_t specVersion, uint16_t *outClkField, uint32_t *outActualHz)
{
	if (baseHz == 0 || targetHz == 0) {
		return false;
	}

	bool tenBit = (specVersion >= kBCM2837SDHCISpecVer300);
	uint32_t maxDiv = tenBit ? 2046u : 256u;
	uint32_t div; /* the real total divisor: actual = base / div */

	if (baseHz <= targetHz) {
		div = 1; /* no division needed; field value 0 */
	} else if (tenBit) {
		for (div = 2; div <= maxDiv; div += 2) {
			if ((uint64_t)baseHz <= (uint64_t)targetHz * div) {
				break;
			}
		}
	} else {
		for (div = 2; div <= maxDiv; div *= 2) {
			if ((uint64_t)baseHz <= (uint64_t)targetHz * div) {
				break;
			}
		}
	}

	if (div > maxDiv) {
		/*
		 * Even the slowest divisor this controller can express is still
		 * faster than the caller asked for. Refuse: the caller's fallback
		 * is by definition safer than a clock above what it asked for.
		 */
		return false;
	}

	uint32_t fieldValue = (div == 1) ? 0u : (div / 2u);
	uint32_t actual = baseHz / div;

	/*
	 * Belt and braces. The loops above already guarantee this, but it is
	 * the one property the whole change's safety rests on, so check it
	 * against the value actually about to be programmed rather than
	 * trusting a loop invariant to survive future edits.
	 */
	if (actual > targetHz) {
		return false;
	}

	uint16_t clk;
	if (tenBit) {
		if (fieldValue > 0x3FF) {
			return false;
		}
		clk = (uint16_t)((fieldValue & 0xFF) << 8);
		clk |= (uint16_t)(((fieldValue & 0x300) >> 8) << 6);
	} else {
		if (fieldValue > 0xFF) {
			return false;
		}
		clk = (uint16_t)((fieldValue & 0xFF) << 8);
	}

	*outClkField = clk;
	*outActualHz = actual;
	return true;
}

#endif /* _BCM2837_SDHCI_DIVIDER_H */
