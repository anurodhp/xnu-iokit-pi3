#include "BCM2837PlatformExpert.h"
#include <IOKit/IODeviceTreeSupport.h>

OSDefineMetaClassAndStructors(BCM2837PlatformExpert, IODTPlatformExpert);

#define super IODTPlatformExpert

/*
 * No start() override. DAR-331 step 1 used to publish "AAPL,boot-display"
 * on this platform expert, but nothing ever registerService()s a platform
 * expert (IOPlatformExpert::start() does not, iokit/Kernel/
 * IOPlatformExpert.cpp:129-195), so no driver could ever match against
 * it. The property now lives on its own "display" device-tree node
 * (devicetrees/rpi3.dts), which createNubs() registers like any other
 * top-level node; IOBootFramebuffer's personality matches that node
 * (iokit/BCM2837ArmIO/BCM2837Personalities.cpp).
 */

// deleteList()/excludeList() are consumed via OSUnserialize() +
// IODTMatchNubWithKeys() (iokit/Kernel/IODeviceTreeSupport.cpp), not a
// plain null-separated C string despite the "const char *" signature -
// "()" is a valid serialized empty OSArray, matching zero node names, so
// deleteList() prunes nothing and excludeList() (used in kIODTExclusive
// mode - "publish everything NOT in this list") excludes nothing, letting
// every real device-tree node through.
const char *
BCM2837PlatformExpert::deleteList(void)
{
	return "()";
}

const char *
BCM2837PlatformExpert::excludeList(void)
{
	return "()";
}

// Trimmed copy of IODTPlatformExpert::processTopLevel() (IOPlatformExpert.cpp)
// with the publishNVRAM()/gIOOptionsEntry assert omitted - see header comment.
void
BCM2837PlatformExpert::processTopLevel(IORegistryEntry *rootEntry)
{
	OSIterator *kids;
	IORegistryEntry *next;
	IORegistryEntry *cpus;

	// infanticide
	kids = IODTFindMatchingEntries(rootEntry, 0, deleteList());
	if (kids) {
		while ((next = (IORegistryEntry *)kids->getNextObject())) {
			next->detachAll(gIODTPlane);
		}
		kids->release();
	}

	// Publish the cpus.
	cpus = rootEntry->childFromPath("cpus", gIODTPlane);
	if (cpus) {
		createNubs(this, IODTFindMatchingEntries(cpus, kIODTExclusive, NULL));
		cpus->release();
	}

	// publish top level, minus excludeList
	createNubs(this, IODTFindMatchingEntries(rootEntry, kIODTExclusive, excludeList()));
}
