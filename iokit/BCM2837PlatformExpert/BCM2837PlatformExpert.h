/*
 * Minimal concrete IODTPlatformExpert subclass for this board.
 *
 * Root-cause context (see BCM2837Personalities.cpp for the fuller story):
 * IODTPlatformExpert (iokit/Kernel/IOPlatformExpert.cpp) is the class that
 * actually walks the device tree and publishes every child node as an
 * IOPlatformDevice nub (via its configure() -> processTopLevel() ->
 * createNubs() chain) - but it's declared OSDeclareAbstractStructors, with
 * deleteList()/excludeList() left pure virtual, so IOKit's matching engine
 * can never instantiate it directly. Without a concrete subclass matched
 * against the root nub, processTopLevel() never runs at all and *no*
 * device-tree child (not just "intc"/"sdhci" - every node) ever gets
 * published as a nub, regardless of what other personalities exist.
 * Confirmed empirically: instrumenting createNubs()/startMatching() showed
 * it was only ever reached once, for "/" itself (via IOPanicPlatform's
 * built-in gIOKernelConfigTables personality, iokit/KernelConfigTables.cpp
 * - the only personality this kernel had before this file existed), never
 * for any device-tree child.
 *
 * This class only needs to exist and answer deleteList()/excludeList() -
 * empty (nothing to prune from this clean, purpose-built synthetic DT) -
 * everything else (probe/configure/createNub/createNubs) is inherited
 * unchanged from IODTPlatformExpert.
 */

#ifndef _BCM2837_PLATFORM_EXPERT_H
#define _BCM2837_PLATFORM_EXPERT_H

#include <IOKit/IOPlatformExpert.h>

class BCM2837PlatformExpert : public IODTPlatformExpert
{
	OSDeclareDefaultStructors(BCM2837PlatformExpert);

public:
	virtual const char *deleteList(void) APPLE_KEXT_OVERRIDE;
	virtual const char *excludeList(void) APPLE_KEXT_OVERRIDE;

	// IODTPlatformExpert::processTopLevel() unconditionally
	// assert(gIOOptionsEntry != NULL) right after publishNVRAM() - its own
	// comment says "subclasses that do their own NVRAM initialization
	// shouldn't be calling this". This board's synthetic device tree has
	// no "/options" (NVRAM) node at all, so this override is a trimmed
	// copy of the base implementation with the NVRAM call/assert omitted
	// rather than faking a nonexistent NVRAM store - see BCM2837PlatformExpert.cpp.
	virtual void processTopLevel(IORegistryEntry *root) APPLE_KEXT_OVERRIDE;

	// DAR-334: reset/halt the board through the PM watchdog. PEHaltRestart()
	// ends in gIOPlatform->haltRestart() (IOPlatformExpert.cpp:1087-1091);
	// until this, PE_halt_restart stayed NULL and every reboot, halt and
	// panic-restart just stopped at "MACH Reboot" until a power cycle.
	virtual bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	virtual int haltRestart(unsigned int type) APPLE_KEXT_OVERRIDE;
};

#endif /* _BCM2837_PLATFORM_EXPERT_H */
