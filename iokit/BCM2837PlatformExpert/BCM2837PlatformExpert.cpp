#include "BCM2837PlatformExpert.h"
#include <IOKit/IODeviceTreeSupport.h>

OSDefineMetaClassAndStructors(BCM2837PlatformExpert, IODTPlatformExpert);

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
