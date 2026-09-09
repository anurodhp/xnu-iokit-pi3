/*
 * BCM2837USBKernelGlue.cpp -- the handful of symbols real IOUSBFamily and
 * AppleUSBCDCDriver reference that this board cannot possibly provide,
 * defined honestly rather than stubbed silently.
 *
 * Vendored into third_party/xnu-7195/iokit/USB/ by
 * tools/vendor_usb_into_xnu.sh. Not compiled by tools/build_kexts.sh --
 * it exists only for the real kernel link.
 *
 * There are exactly two groups, both identified by running `nm` over the
 * built objects rather than by guessing:
 *
 *   1. IOPCIDevice -- 5 symbols. BCM2837 has NO PCI BUS.
 *   2. kmod_info   -- 1 symbol. This kernel has no kext-bundle pipeline.
 *
 * ===================== 1. IOPCIDevice ==============================
 *
 * IOUSBControllerV3.h declares `IOPCIDevice *_device` and both
 * IOUSBController.cpp and IOUSBControllerV3.cpp do
 * `OSDynamicCast(IOPCIDevice, provider)` to find the PCI function their
 * host controller sits behind, then read PCI config space through it.
 * On a Mac that is how a USB controller finds its own hardware. On a
 * Raspberry Pi the USB controller is a memory-mapped SoC peripheral
 * described by the device tree, reached through BCM2837DWC2, and there is
 * no PCI bus anywhere in the machine.
 *
 * The obvious-looking shortcut here is a real trap, and is why this file
 * is longer than it looks like it should be:
 *
 *     const OSMetaClass * const IOPCIDevice::metaClass = 0;   // WRONG
 *
 * That links, and panics the first time anything runs. OSDynamicCast
 * expands to OSMetaClassBase::safeMetaCast(obj, IOPCIDevice::metaClass),
 * and while safeMetaCast does null-check its FIRST argument
 * (`return (me) ? me->metaCast(toType) : NULL;`, OSMetaClass.cpp:295) the
 * metaCast it calls does NOT null-check the second:
 *
 *     OSMetaClassBase::metaCast(const OSMetaClass *toMeta) const
 *     { return toMeta->checkMetaCast(this); }        // :385-388
 *
 * So a null metaClass is a null-deref on the first cast against any
 * non-null provider -- i.e. exactly when a USB controller starts, which
 * is the only time this code runs at all.
 *
 * What is provided instead is a real, registered-but-uninstantiable
 * OSMetaClass named "IOPCIDevice". Every OSDynamicCast against it walks
 * the candidate's real class chain, never finds this metaclass (nothing
 * in this kernel derives from IOPCIDevice, and alloc() returns NULL so
 * nothing can), and cleanly returns NULL. That is the correct answer on
 * this board: there is no PCI device behind the USB controller. The
 * family's own code already handles _device == NULL -- it has to, since
 * Apple ships USB controllers on non-PCI transports too.
 *
 * Using the normal OSDefineMetaClassAndStructors macro is not an option:
 * it defines IOPCIDevice's constructor and destructor, which forces the
 * class's vtable to be emitted, which requires a definition for every one
 * of the ~50 virtuals IOPCIDevice.h declares. Defining a minimal
 * OSMetaClass subclass by hand gets the one thing that is actually needed
 * -- a valid, non-null, never-matching metaclass -- without pulling in
 * IOPCIFamily's implementation.
 *
 * The four extendedConfig* methods are NON-virtual members (IOPCIDevice.h
 * :732-764), reached through the inline configRead16/32 / configWrite16/32
 * wrappers the family calls. Defining them emits exactly those four
 * symbols and does not require the vtable. They are unreachable -- the
 * only way to obtain an IOPCIDevice* is the OSDynamicCast above, which
 * always yields NULL -- so each one panics rather than returning a
 * plausible value. A stub returning 0 would let a future caller silently
 * read "PCI config space" full of zeroes and act on it.
 *
 * ================== 2. kmod_info -- NOT defined here ===============
 *
 * AppleUSBCDCECMData.cpp includes linkup.h, whose LinkUpMessage() helper
 * reads KMOD_INFO_NAME.name to put the driver's own name in its link-up
 * log line. kmod_info is the structure the kext loader fills in for a
 * driver loaded as a .kext bundle, and this project compiles its drivers
 * directly into the kernel with no kext-bundle/prelink pipeline at all
 * (CLAUDE.md) -- so it looked like a symbol this file had to provide,
 * and an earlier version of it did.
 *
 * That was wrong, and the kernel link says so:
 *
 *     duplicate symbol '_kmod_info' in:
 *         .../justlto.o
 *         command-line-aliases-file
 *
 * XNU already solves this exact problem, and solves it better. Its
 * config/Libkern.exports:727 carries the alias
 *
 *     _kmod_info:_invalid_kmod_info
 *
 * pointing at a real struct in libkern/c++/OSKext.cpp:380 whose own
 * comment is: "Set up a fake kmod_info struct for statically linked
 * kexts that don't have one." That is precisely what these drivers are
 * here. It is fully populated (.name = "invalid", .version = "0",
 * .id = UINT32_MAX), so linkup.h's strchr() walk over .name is safe.
 *
 * So: define nothing. The link-up log line will read "invalid" rather
 * than "AppleUSBCDCECMData", which is cosmetic and, if anything, more
 * truthful -- there is no kext bundle here to name.
 *
 * Left documented rather than silently deleted because "this kernel has
 * no kmod_info" is the obvious-looking conclusion, it is wrong, and the
 * only thing that disproves it is a duplicate-symbol error at the very
 * end of a ~20-minute kernel link.
 */

#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSMetaClass.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <mach/mach_types.h>
#include <libkern/OSKextLib.h>

/* ------------------------------------------------------------------ */
/* 1. IOPCIDevice                                                      */
/* ------------------------------------------------------------------ */

namespace {

class AbsentIOPCIDeviceMetaClass : public OSMetaClass
{
public:
	AbsentIOPCIDeviceMetaClass() :
		OSMetaClass("IOPCIDevice", OSTypeID(IOService), sizeof(IOService))
	{
	}

	/*
	 * Pure virtual on OSMetaClass. Returning NULL is what makes this
	 * class uninstantiable, which is in turn what guarantees every
	 * OSDynamicCast against it returns NULL.
	 */
	virtual OSObject *alloc(void) const APPLE_KEXT_OVERRIDE
	{
		return NULL;
	}
};

static AbsentIOPCIDeviceMetaClass gAbsentIOPCIDeviceMetaClass;

} /* anonymous namespace */

const OSMetaClass * const IOPCIDevice::metaClass = &gAbsentIOPCIDeviceMetaClass;

UInt32
IOPCIDevice::extendedConfigRead32(IOByteCount offset)
{
	panic("IOPCIDevice::extendedConfigRead32(0x%llx) on a machine with no "
	    "PCI bus - see iokit/USB/BCM2837USBKernelGlue.cpp",
	    (unsigned long long)offset);
	return 0;
}

UInt16
IOPCIDevice::extendedConfigRead16(IOByteCount offset)
{
	panic("IOPCIDevice::extendedConfigRead16(0x%llx) on a machine with no "
	    "PCI bus - see iokit/USB/BCM2837USBKernelGlue.cpp",
	    (unsigned long long)offset);
	return 0;
}

void
IOPCIDevice::extendedConfigWrite32(IOByteCount offset, UInt32 data)
{
	panic("IOPCIDevice::extendedConfigWrite32(0x%llx, 0x%x) on a machine "
	    "with no PCI bus - see iokit/USB/BCM2837USBKernelGlue.cpp",
	    (unsigned long long)offset, data);
}

void
IOPCIDevice::extendedConfigWrite16(IOByteCount offset, UInt16 data)
{
	panic("IOPCIDevice::extendedConfigWrite16(0x%llx, 0x%x) on a machine "
	    "with no PCI bus - see iokit/USB/BCM2837USBKernelGlue.cpp",
	    (unsigned long long)offset, data);
}

/* ------------------------------------------------------------------ */
/* 2. kmod_info -- deliberately absent; see the header comment.         */
/* ------------------------------------------------------------------ */
