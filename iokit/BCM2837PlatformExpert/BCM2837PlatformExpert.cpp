#include "BCM2837PlatformExpert.h"
#include <IOKit/IODeviceTreeSupport.h>

OSDefineMetaClassAndStructors(BCM2837PlatformExpert, IODTPlatformExpert);

#define super IODTPlatformExpert

/*
 * DAR-331 step 1: publish "AAPL,boot-display" on this nub, the same
 * property real IOGraphicsFamily's IOBootFramebuffer::probe() looks for on
 * ITS provider before it will ever match (see the header comment for the
 * exact citation). IOBootFramebuffer is not part of this kernel build yet
 * (DAR-331's own status note: IOFramebuffer.cpp alone is ~15K lines with
 * ACPI/IOHibernatePrivate/RootDomain power-management coupling this board
 * does not have - real remaining work, not a one-line wire-up), so nothing
 * matches against this property today. It is still worth setting now,
 * ahead of that: it is real, checkable state (`ioreg` on this nub after a
 * real HDMI-attached boot vs. a headless one), it is a genuine prerequisite
 * IOBootFramebuffer::probe() hard-requires, and setting it here - in the
 * one platform-expert class this board actually has, at the point it
 * already knows the answer - is where real Apple platform code sets this
 * kind of property too (a platform driver publishing it on its own root
 * nub once boot video is known good), not a synthetic stand-in.
 *
 * getConsoleInfo() (IOPlatformExpert.h:276) reads PE_state.video, which
 * pexpert/arm/pe_init.c's bcm2837_video_init() already populated from the
 * VideoCore mailbox long before any IOKit matching runs - v_baseAddr != 0
 * is exactly IOBootFramebuffer::probe()'s own success condition
 * (third_party/IOGraphics/IOGraphicsFamily/IOBootFramebuffer.cpp:53-55),
 * checked here rather than assumed so a headless/no-HDMI boot (v_baseAddr
 * == 0, PE_initialize_console() having fallen back to kPETextMode - see
 * pe_init.c:774-777) does not publish a property promising a framebuffer
 * that was never found.
 */
bool
BCM2837PlatformExpert::start(IOService *provider)
{
	if (!super::start(provider)) {
		return false;
	}

	PE_Video bootDisplay;
	if (getConsoleInfo(&bootDisplay) == kIOReturnSuccess && bootDisplay.v_baseAddr != 0) {
		setProperty("AAPL,boot-display", kOSBooleanTrue);
	}

	return true;
}

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
