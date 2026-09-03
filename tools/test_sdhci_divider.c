/*
 * Host-side exhaustive test for BCM2837SDHCI's SD clock divider math
 * (DAR-103). Run via tools/test_sdhci_divider.sh.
 *
 * This compiles the REAL shipping function - iokit/BCM2837SDHCI/
 * BCM2837SDHCIDivider.h is included directly, not re-implemented here -
 * because the property under test ("never programs a clock faster than
 * asked for") is exactly the property a re-implementation would get
 * wrong in the same way the original did.
 *
 * Why a host test and not just a QEMU boot: QEMU's SDHCI model
 * (third_party/qemu-zhuowei/hw/sd/sdhci.c) stores CLOCK_CONTROL and never
 * uses the divider field for anything - MASKED_WRITE into s->clkcon, then
 * only the INT_EN/INT_STABLE bits are looked at. So a QEMU boot can prove
 * the driver still works end-to-end, but it can NEVER catch a wrong
 * divisor: emulated transfers are untimed. The arithmetic therefore has
 * to be verified somewhere that actually checks it, which is here.
 *
 * The checks, for both divider encodings and a wide sweep of base clocks:
 *   1. Whenever the function succeeds, the rate it reports is <= the
 *      requested target. (The safety property. A violation is a
 *      too-fast SD clock, i.e. potential data corruption.)
 *   2. The bit-packed field it returns decodes - by an independent
 *      decoder written from the SDHCI spec's field layout, not by
 *      calling back into the function - to exactly the rate it reported.
 *      Catches a correct rate packed into the wrong bits.
 *   3. In 8-bit (spec 1.00/2.00) mode the field is a legal encoding:
 *      at most one bit set.
 *   4. It returns the FASTEST expressible rate <= target, i.e. the next
 *      divisor step up would have exceeded the target. Catches a
 *      "safe but pointlessly slow" regression, which would look fine
 *      to check 1 forever.
 *   5. Spot values computed by hand for the two base clocks that
 *      actually matter (QEMU's 50MHz, a real Pi's ~250MHz).
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "../iokit/BCM2837SDHCI/BCM2837SDHCIDivider.h"

static int failures;

#define CHECK(cond, fmt, ...)                                            \
	do {                                                             \
		if (!(cond)) {                                           \
			failures++;                                      \
			printf("FAIL %s:%d: " fmt "\n", __func__,        \
			    __LINE__, ##__VA_ARGS__);                    \
		}                                                        \
	} while (0)

/*
 * Independent decoder: given the CLOCK_CONTROL value the driver would
 * write, recover the total divisor the hardware will apply. Written
 * straight from the SDHCI spec's field layout (8-bit SDCLK_FREQ_SEL at
 * [15:8]; in spec 3.00 the upper 2 bits of a 10-bit value at [7:6]),
 * with the "field value N means base/(2N), N==0 means base/1" rule.
 */
static uint32_t
decodeDivisor(uint16_t clkField, bool tenBit)
{
	uint32_t fieldValue = (uint32_t)((clkField >> 8) & 0xFF);
	if (tenBit) {
		fieldValue |= (uint32_t)(((clkField >> 6) & 0x3) << 8);
	}
	return (fieldValue == 0) ? 1u : (fieldValue * 2u);
}

static void
checkOne(uint32_t baseHz, uint32_t targetHz, uint16_t specVersion)
{
	bool tenBit = (specVersion >= kBCM2837SDHCISpecVer300);
	uint16_t clkField = 0xDEAD;
	uint32_t actualHz = 0xDEADBEEF;

	if (!bcm2837SDHCICalcDivider(baseHz, targetHz, specVersion, &clkField,
	    &actualHz)) {
		/*
		 * A refusal is always safe, but it must only happen when it
		 * genuinely has to: with the largest divisor the encoding
		 * allows, the rate must still exceed the target.
		 */
		uint32_t maxDiv = tenBit ? 2046u : 256u;
		CHECK((uint64_t)baseHz > (uint64_t)targetHz * maxDiv,
		    "refused base=%u target=%u spec=%u but div=%u would have worked "
		    "(%u Hz)", baseHz, targetHz, specVersion, maxDiv, baseHz / maxDiv);
		return;
	}

	/* 1. THE safety property. */
	CHECK(actualHz <= targetHz,
	    "base=%u target=%u spec=%u -> actual=%u EXCEEDS target",
	    baseHz, targetHz, specVersion, actualHz);

	/* 2. The bits actually written decode back to that same rate. */
	uint32_t div = decodeDivisor(clkField, tenBit);
	CHECK(baseHz / div == actualHz,
	    "base=%u target=%u spec=%u -> field %#x decodes to div=%u (%u Hz), "
	    "but function reported %u Hz",
	    baseHz, targetHz, specVersion, clkField, div, baseHz / div, actualHz);

	/* 2b. Nothing outside the divider field may be set. */
	uint16_t allowed = tenBit ? (uint16_t)0xFFC0 : (uint16_t)0xFF00;
	CHECK((clkField & (uint16_t)~allowed) == 0,
	    "base=%u target=%u spec=%u -> field %#x sets bits outside the "
	    "divider", baseHz, targetHz, specVersion, clkField);

	/* 3. 8-bit mode: legal encodings have at most one bit set. */
	if (!tenBit) {
		uint32_t fieldValue = (uint32_t)((clkField >> 8) & 0xFF);
		CHECK((fieldValue & (fieldValue - 1)) == 0,
		    "base=%u target=%u spec=200 -> 8-bit field value %#x has more "
		    "than one bit set (illegal encoding)",
		    baseHz, targetHz, fieldValue);
		CHECK(div == 1 || (div & (div - 1)) == 0,
		    "base=%u target=%u spec=200 -> divisor %u is not a power of two",
		    baseHz, targetHz, div);
		CHECK(div <= 256, "base=%u target=%u spec=200 -> divisor %u > 256",
		    baseHz, targetHz, div);
	} else {
		CHECK(div == 1 || (div % 2) == 0,
		    "base=%u target=%u spec=300 -> divisor %u is odd",
		    baseHz, targetHz, div);
		CHECK(div <= 2046, "base=%u target=%u spec=300 -> divisor %u > 2046",
		    baseHz, targetHz, div);
	}

	/* 4. It is the fastest expressible rate that still fits. */
	if (div > 1) {
		uint32_t nextFaster = tenBit ? (div == 2 ? 1 : div - 2) : div / 2;
		CHECK((uint64_t)baseHz > (uint64_t)targetHz * nextFaster,
		    "base=%u target=%u spec=%u -> chose div=%u, but div=%u (%u Hz) "
		    "also fits and is faster",
		    baseHz, targetHz, specVersion, div, nextFaster, baseHz / nextFaster);
	}
}

static void
sweep(void)
{
	static const uint32_t targets[] = {
		400000,    /* kIdentificationClockHz */
		25000000,  /* kDefaultSpeedClockHz */
		100000, 1000000, 12500000, 50000000,
	};
	static const uint16_t specs[] = {
		kBCM2837SDHCISpecVer100,
		kBCM2837SDHCISpecVer200,
		kBCM2837SDHCISpecVer300,
	};

	unsigned long cases = 0;
	/* The whole plausibility window readClockConfig() accepts, 1MHz..1GHz. */
	for (uint32_t base = 1000000; base <= 1000000000u; base += 250000) {
		for (size_t t = 0; t < sizeof(targets) / sizeof(targets[0]); t++) {
			for (size_t s = 0; s < sizeof(specs) / sizeof(specs[0]); s++) {
				checkOne(base, targets[t], specs[s]);
				cases++;
			}
		}
	}
	/* Plus every base clock the Pi firmware is actually known to report. */
	static const uint32_t realBases[] = {
		41666667, 50000000, 62500000, 100000000, 200000000, 250000000,
		400000000,
	};
	for (size_t b = 0; b < sizeof(realBases) / sizeof(realBases[0]); b++) {
		for (size_t t = 0; t < sizeof(targets) / sizeof(targets[0]); t++) {
			for (size_t s = 0; s < sizeof(specs) / sizeof(specs[0]); s++) {
				checkOne(realBases[b], targets[t], specs[s]);
				cases++;
			}
		}
	}
	printf("swept %lu (base, target, spec) combinations\n", cases);
}

static void
spotValues(void)
{
	uint16_t clk = 0;
	uint32_t hz = 0;

	/*
	 * QEMU: bcm2835_property.c answers 50000000 for clock id 1 (EMMC),
	 * and bcm2835_peripherals.c sets sd-spec-version=3.
	 * 400kHz: smallest even div with 50e6/div <= 400000 is 126 ->
	 *   396825 Hz, field value 63 = 0x3F -> CLOCK_CONTROL 0x3F00.
	 */
	CHECK(bcm2837SDHCICalcDivider(50000000, 400000, kBCM2837SDHCISpecVer300,
	    &clk, &hz), "QEMU 50MHz/400kHz should succeed");
	CHECK(hz == 396825 && clk == 0x3F00,
	    "QEMU 50MHz/400kHz -> %u Hz field %#x (expected 396825 / 0x3f00)",
	    hz, clk);

	/* 25MHz: div 2 -> exactly 25MHz, field value 1 -> 0x0100. */
	CHECK(bcm2837SDHCICalcDivider(50000000, 25000000, kBCM2837SDHCISpecVer300,
	    &clk, &hz), "QEMU 50MHz/25MHz should succeed");
	CHECK(hz == 25000000 && clk == 0x0100,
	    "QEMU 50MHz/25MHz -> %u Hz field %#x (expected 25000000 / 0x0100)",
	    hz, clk);

	/*
	 * A real Pi 3 commonly reports a 250MHz EMMC clock.
	 * 400kHz: smallest even div with 250e6/div <= 400000 is 626 ->
	 *   399361 Hz, field value 313 = 0x139 -> low 8 bits 0x39 at [15:8],
	 *   high 2 bits 0b01 at [7:6] -> 0x3940.
	 */
	CHECK(bcm2837SDHCICalcDivider(250000000, 400000, kBCM2837SDHCISpecVer300,
	    &clk, &hz), "250MHz/400kHz should succeed on spec 3.00");
	CHECK(hz == 399361 && clk == 0x3940,
	    "250MHz/400kHz -> %u Hz field %#x (expected 399361 / 0x3940)", hz, clk);

	/* 25MHz from 250MHz: div 10 -> field value 5 -> 0x0500. */
	CHECK(bcm2837SDHCICalcDivider(250000000, 25000000, kBCM2837SDHCISpecVer300,
	    &clk, &hz), "250MHz/25MHz should succeed on spec 3.00");
	CHECK(hz == 25000000 && clk == 0x0500,
	    "250MHz/25MHz -> %u Hz field %#x (expected 25000000 / 0x0500)", hz, clk);

	/*
	 * The case that MUST refuse: 8-bit mode caps the divisor at 256, so a
	 * 250MHz base cannot be brought to 400kHz at all (250e6/256 = 976kHz).
	 * Returning the closest-anyway value here would put the card
	 * identification sequence at 2.4x the spec's maximum.
	 */
	CHECK(!bcm2837SDHCICalcDivider(250000000, 400000, kBCM2837SDHCISpecVer200,
	    &clk, &hz), "250MHz/400kHz on spec 2.00 must refuse, not approximate");

	/* Degenerate inputs must refuse rather than divide by zero. */
	CHECK(!bcm2837SDHCICalcDivider(0, 400000, kBCM2837SDHCISpecVer300, &clk, &hz),
	    "zero base clock must refuse");
	CHECK(!bcm2837SDHCICalcDivider(50000000, 0, kBCM2837SDHCISpecVer300, &clk, &hz),
	    "zero target must refuse");

	/* Base slower than the target: no division, and never faster than base. */
	CHECK(bcm2837SDHCICalcDivider(10000000, 25000000, kBCM2837SDHCISpecVer300,
	    &clk, &hz), "10MHz base / 25MHz target should succeed");
	CHECK(hz == 10000000 && clk == 0x0000,
	    "10MHz/25MHz -> %u Hz field %#x (expected 10000000 / 0x0000)", hz, clk);
}

int
main(void)
{
	spotValues();
	sweep();

	if (failures != 0) {
		printf("FAILED: %d check(s)\n", failures);
		return 1;
	}
	printf("OK: all SD clock divider checks passed\n");
	return 0;
}
