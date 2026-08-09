#include "BCM2837ArmIO.h"

#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>

#include <string.h>

#define super IOService

OSDefineMetaClassAndStructors(BCM2837ArmIO, IOService);

IOService *
BCM2837ArmIO::probe(IOService *provider, SInt32 *score)
{
	IOService *result = super::probe(provider, score);
	if (result == NULL) {
		return NULL;
	}
	const char *name = provider->getName();
	if (name == NULL || strcmp(name, "arm-io") != 0) {
		return NULL;
	}
	return result;
}

bool
BCM2837ArmIO::start(IOService *provider)
{
	if (!super::start(provider)) {
		return false;
	}

	IODTPlatformExpert *pe = OSDynamicCast(IODTPlatformExpert, getPlatform());
	if (pe == NULL) {
		IOLog("BCM2837ArmIO: getPlatform() is not an IODTPlatformExpert, cannot publish children\n");
		return false;
	}

	// Same mechanism IODTPlatformExpert::processTopLevel() uses for the DT
	// root's own top-level nodes, just aimed at our own provider (the
	// "arm-io" nub) instead - publishes "gpio"/"aux"/"intc"/"sdhci"/"mbox"
	// as real IOPlatformDevice nubs.
	//
	// `pe`, not `this`, as the parent: IOPlatformDevice::getResources()
	// (iokit/Kernel/IOPlatformExpert.cpp) does
	// `((IOPlatformExpert *)getProvider())->getNubResources(this)` -
	// blindly casting whatever it's attached to in gIOServicePlane to
	// IOPlatformExpert. Passing `this` (a plain IOService, not an
	// IOPlatformExpert) here would make that a bogus cast/vtable-mismatch
	// call for every child, silently breaking IODTResolveAddressing() for
	// all of them (confirmed empirically: getDeviceMemoryCount() was 0 for
	// "intc" until this fix). processTopLevel() itself sets exactly this
	// precedent for its "cpus" children - gIOServicePlane is deliberately
	// flat relative to the platform expert regardless of how nested a
	// device is in gIODTPlane.
	pe->createNubs(pe, IODTFindMatchingEntries(provider, kIODTExclusive, NULL));

	IOLog("BCM2837ArmIO: started, published children of \"%s\"\n",
	    provider->getName() ? provider->getName() : "?");

	// Deliberately no registerService() here: this is a terminal bus-
	// enumeration leaf (its whole job is publishing its DT children above,
	// which are separately matchable nubs) - nothing needs to match
	// *against* "BCM2837ArmIO" itself as a provider, so there's no reason
	// to publish it for further matching. (Empirically: calling
	// registerService() here hit a real pre-existing bug in this xnu-7195
	// vintage - IOService::probeCandidates()/doServiceMatch() reaching
	// OSMetaClass::serialize(), which is unconditionally
	// panic("...Obsoleted") in this source tree. Not calling
	// registerService() on a childless leaf avoids it entirely, which is
	// also just the architecturally correct thing to do here regardless.)
	return true;
}
