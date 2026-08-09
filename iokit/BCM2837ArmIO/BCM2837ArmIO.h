/*
 * Minimal driver for the "arm-io" device-tree node - the SoC peripheral
 * bus container that "gpio"/"aux"/"intc"/"sdhci"/"mbox" all live under
 * (devicetrees/rpi3.dts), matching real Apple ARM SoC device trees' own
 * "arm-io" convention.
 *
 * Root-cause context: IODTPlatformExpert::processTopLevel() only ever
 * walks the DT root's own immediate children (plus "cpus"' immediate
 * children as a special case) - it does not recurse into other container
 * nodes. In real IOKit, a bus/container node's own children are published
 * by whatever driver matches and starts *that* container node, not by the
 * platform expert - that's a normal driver's job, cascading down one
 * level at a time. Without a driver for "arm-io" itself, nothing ever
 * calls createNubs() for its children, so "intc"/"sdhci" (and "gpio"/
 * "aux"/"mbox") never get nubs at all, regardless of their own
 * personalities - confirmed empirically (createNubs() diagnostic logging
 * showed "arm-io" itself getting a nub/registerService(), but never its
 * children).
 *
 * This driver does the minimum needed: reuses the platform expert's own
 * createNubs()/IODTFindMatchingEntries() (both public on IODTPlatformExpert
 * already, iokit/Kernel/IOPlatformExpert.cpp) against its own provider (the
 * "arm-io" nub) to publish its children exactly the same way the platform
 * expert publishes top-level nodes - no new nub-creation logic needed.
 */

#ifndef _BCM2837_ARM_IO_H
#define _BCM2837_ARM_IO_H

#include <IOKit/IOService.h>

class BCM2837ArmIO : public IOService
{
	OSDeclareDefaultStructors(BCM2837ArmIO);

public:
	// Defensive re-check that provider is actually named "arm-io",
	// mirroring IODTPlatformExpert::probe()'s own compareNames()
	// re-validation (iokit/Kernel/IOPlatformExpert.cpp) - empirically
	// necessary: IOService::matchInternal()'s generic IONameMatch
	// handling was observed matching this personality against every
	// sibling under "arm-io" (mbox/sdhci/intc/aux/gpio), not just
	// "arm-io" itself, in this xnu-7195 build (confirmed by instance-
	// pointer-tagged diagnostic logging - genuinely separate match
	// attempts, not a logging artifact). Root-level matching never hit
	// this because IODTPlatformExpert already does exactly this kind of
	// defensive check; plain IOService subclasses relying solely on the
	// personality-table IONameMatch key do not get it for free.
	virtual IOService *probe(IOService *provider, SInt32 *score) APPLE_KEXT_OVERRIDE;

	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
};

#endif /* _BCM2837_ARM_IO_H */
