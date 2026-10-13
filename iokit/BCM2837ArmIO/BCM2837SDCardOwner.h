/*
 * Which controller owns the SD card (GPIO48-53) under boot-arg wifi=1
 * (docs/wifi-plan.md). BCM2837SDHost and BCM2837SDHCI both run in this one
 * kernel image and race through IOKit matching on separate threads, so the
 * decision is one compare-and-swap on a shared word, taken before either
 * driver writes a single GPIO48-53 function bit:
 *
 *   - BCM2837SDHost claims kOwnerSDHost before pinmuxing. If the claim fails
 *     (BCM2837SDHCI already gave up waiting and took the card), it declines.
 *   - If its card init fails it hands the card back: release(kOwnerSDHost ->
 *     kOwnerSDHCI), after restoring the pins.
 *   - BCM2837SDHCI waits for the published verdict; on timeout it claims
 *     kOwnerSDHCI itself. If that claim fails, sdhost holds the card and its
 *     verdict is coming, so it keeps waiting.
 *
 * The inline function's static is a single object across translation units
 * (C++ [dcl.inline]: a static local in an inline function with external
 * linkage refers to the same object in every TU).
 */
#ifndef _BCM2837SDCARDOWNER_H
#define _BCM2837SDCARDOWNER_H

#include <libkern/OSAtomic.h>

class BCM2837SDCardOwner {
public:
	enum : UInt32 {
		kOwnerNone   = 0,
		kOwnerSDHost = 1,
		kOwnerSDHCI  = 2,
	};

	static bool claim(UInt32 who)
	{
		return OSCompareAndSwap(kOwnerNone, who, word());
	}

	// sdhost failed: give the card to the Arasan controller.
	static bool handBack(void)
	{
		return OSCompareAndSwap(kOwnerSDHost, kOwnerSDHCI, word());
	}

	static UInt32 owner(void)
	{
		return *word();
	}

private:
	static volatile UInt32 *word(void)
	{
		static volatile UInt32 sOwner = kOwnerNone;
		return &sOwner;
	}
};

#endif /* _BCM2837SDCARDOWNER_H */
