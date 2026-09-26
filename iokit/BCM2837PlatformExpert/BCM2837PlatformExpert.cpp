#include "BCM2837PlatformExpert.h"
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOLib.h>

OSDefineMetaClassAndStructors(BCM2837PlatformExpert, IODTPlatformExpert);

#define super IODTPlatformExpert

/*
 * start() only maps the PM watchdog for haltRestart() (DAR-334, below).
 * It publishes nothing: DAR-331 step 1 used to publish "AAPL,boot-display"
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

#pragma mark - Reset and halt (DAR-334)

/*
 * The BCM2835-family PM block's watchdog is how this SoC resets itself:
 * there is no other reset controller. Register layout and sequence are
 * u-boot's (third_party/u-boot/arch/arm/mach-bcm283x/include/mach/wdog.h
 * and reset.c:32-49, __reset_cpu(), the code that resets this exact board): block at
 * peripheral base + 0x100000, RSTC at +0x1c, RSTS at +0x20, WDOG at +0x24,
 * every write tagged with the 0x5a000000 password. The peripheral base is
 * 0x3f000000 on BCM2837 (the "arm-io" ranges; BCM2837Mailbox.cpp's own
 * cross-check).
 *
 * Halt is the same reset after marking RSTS with the Raspberry Pi
 * firmware's "halt" partition (0x555): bootcode.bin then stays halted
 * instead of booting (u-boot reset.c:72-82, the EFI_RESET_SHUTDOWN branch).
 *
 * Mapped once at start() so the panic-restart path never allocates.
 *
 * Panics reset too (kPEPanicRestartCPU*), as a Mac restarts after a panic.
 * Before this, PE_halt_restart was NULL and a panic spun in halt_all_cpus()
 * with the message on screen. The trade-off, chosen deliberately (DAR-334
 * acceptance: "A kernel panic reboots the Pi"): a panic that recurs on
 * every boot now loops through the firmware instead of staying on screen.
 * The panic text still goes out on serial first (debug.c's panic path
 * prints it, then debug.c:1263-1264 calls kdp_machine_reboot_type() only
 * when panicDebugging is off; debug=0x4 (DB_NMI) keeps a panic spinning
 * instead, debug.c:344-346). The watchdog fires ~150 us after the write, so
 * the last UART FIFO of output can be cut. tools/boot_test.py fails on
 * any panic by default, so the reboot cannot hide one from a test.
 */
enum {
	kPMPhysBase         = 0x3f100000,
	kPMRegRSTC          = 0x1c / 4,
	kPMRegRSTS          = 0x20 / 4,
	kPMRegWDOG          = 0x24 / 4,
	kPMPassword         = 0x5a000000,
	kPMRSTCWrcfgMask    = 0x00000030,
	kPMRSTCFullReset    = 0x00000020,
	kPMRSTSHaltPartition = 0x00000555,
	kPMResetTicks       = 10,       // u-boot reset.c:16 RESET_TIMEOUT
};

static volatile uint32_t *sPMRegs;

bool
BCM2837PlatformExpert::start(IOService *provider)
{
	if (!super::start(provider)) {
		return false;
	}
	IODeviceMemory *mem = IODeviceMemory::withRange(kPMPhysBase, 0x1000);
	IOMemoryMap *map = (mem != NULL) ? mem->map() : NULL;
	if (map != NULL) {
		sPMRegs = (volatile uint32_t *)map->getVirtualAddress();
		// The map is kept for the life of the system, like the mailbox's.
	} else {
		IOLog("BCM2837PlatformExpert: PM watchdog map failed - reboot/halt "
		    "will hang instead of resetting\n");
	}
	OSSafeReleaseNULL(mem);
	return true;
}

static void
pmWatchdogReset(bool halt)
{
	if (halt) {
		sPMRegs[kPMRegRSTS] = kPMPassword | sPMRegs[kPMRegRSTS] | kPMRSTSHaltPartition;
	}
	uint32_t rstc = sPMRegs[kPMRegRSTC];
	rstc &= ~kPMRSTCWrcfgMask;
	rstc |= kPMRSTCFullReset;
	sPMRegs[kPMRegWDOG] = kPMPassword | kPMResetTicks;
	sPMRegs[kPMRegRSTC] = kPMPassword | rstc;
	__asm__ volatile ("dsb sy" ::: "memory");
	for (;;) {
		__asm__ volatile ("wfi");
	}
}

int
BCM2837PlatformExpert::haltRestart(unsigned int type)
{
	if (sPMRegs == NULL) {
		return super::haltRestart(type);
	}
	switch (type) {
	case kPERestartCPU:
	case kPEPanicRestartCPU:
	case kPEPanicRestartCPUNoCallouts:
		pmWatchdogReset(false);
		break;
	case kPEHaltCPU:
	case kPEUPSDelayHaltCPU:
		pmWatchdogReset(true);
		break;
	default:
		break;
	}
	return super::haltRestart(type);
}
