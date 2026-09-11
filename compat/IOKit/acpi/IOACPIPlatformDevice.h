/*
 * <IOKit/acpi/IOACPIPlatformDevice.h> -- minimal ARM/device-tree stand-in.
 *
 * WHY THIS FILE EXISTS
 *
 * IOUSBFamily's IOUSBController.h/.cpp `#include
 * <IOKit/acpi/IOACPIPlatformDevice.h>` unconditionally, and
 * IOUSBController.cpp's last ~700 lines (CopyACPIDevice(),
 * HasExpressCardUSB(), IsControllerMuxed(), CheckACPITablesForCaptiveRootHubPorts(),
 * ...) walk the "IOACPIPlane" registry plane looking for ACPI `_EJD`/`_UPC`/
 * `MUXS`/`XHCN` objects to decide whether a given root-hub port is an
 * ExpressCard slot, is captive/internal, or is muxed to another controller.
 * That is Intel-Mac firmware plumbing: it exists to read the port topology
 * out of the machine's ACPI DSDT.
 *
 * IOACPIFamily is NOT open source (Apple ships only its headers in the
 * Kernel framework, never the sources), so there is no real version of this
 * header to pin the way third_party/ pins everything else -- and, per
 * CLAUDE.md's source-priority rule, no equivalent upstream to fall back to
 * either, because the thing being modelled is Apple's own IOKit-over-ACPI
 * binding rather than a standard.
 *
 * BCM2837 has no ACPI at all. This board is described entirely by a real
 * device tree (devicetrees/rpi3.dts), and this kernel has no ACPI parser,
 * no IOACPIPlatformExpert, and no "IOACPIPlane" in its registry. So every
 * one of those IOUSBController code paths is *correctly* inert here:
 * `getPlane("IOACPIPlane")` returns NULL, `metaCast("IOACPIPlatformDevice")`
 * returns NULL for every entry (nothing in this kernel ever registers that
 * class), and each function falls through to its "no ACPI info" default --
 * which for a Pi is the right answer (no ExpressCard slot, no mux, ports
 * are what the DWC2 root hub says they are).
 *
 * This header therefore declares exactly the surface IOUSBController.cpp
 * actually calls -- validateObject(), evaluateObject(), evaluateInteger(),
 * plus IORegistryEntry's own getPath()/getLocation()/getPlane(), which come
 * from the real IOService base -- and nothing else. The methods are declared
 * but never defined and never called at runtime, because no instance of this
 * class can exist in this kernel: no code path constructs one, and
 * metaCast() is the only way IOUSBController obtains a pointer. Declaring
 * them without OSDeclareDefaultStructors is deliberate; adding the metaclass
 * machinery would register a real, always-empty class in the kernel's
 * OSMetaClass table and make `metaCast("IOACPIPlatformDevice")` a slightly
 * less obviously-dead check, for no gain.
 *
 * Signatures copied from Apple's real published IOACPIPlatformDevice.h as it
 * appears in the macOS Kernel.framework headers (the class's method
 * signatures are public API, even though its implementation is not), NOT
 * invented to fit the call sites.
 *
 * If a future platform here ever grows real ACPI (it will not -- this is a
 * Raspberry Pi), this file is the thing to delete, not extend.
 */

#ifndef _IOKIT_ACPI_IOACPIPLATFORMDEVICE_H_COMPAT
#define _IOKIT_ACPI_IOACPIPLATFORMDEVICE_H_COMPAT

#include <IOKit/IOService.h>
#include <libkern/c++/OSObject.h>

/* Real IOACPIFamily declares these; only kIOACPIPlane is referenced here. */
#define kIOACPIPlane "IOACPIPlane"

class IOACPIPlatformDevice : public IOService
{
public:
	/*
	 * Returns kIOReturnSuccess if the named ACPI object exists in this
	 * device's scope. IOUSBController uses it as an existence probe for
	 * "_EJD" / "_UPC" / "MUXS" / "HERB" before evaluating them.
	 */
	virtual IOReturn validateObject(const char *objectName);

	/*
	 * Evaluates the named ACPI object, returning the result as an
	 * OSObject (typically an OSArray of OSNumbers for _UPC).
	 */
	virtual IOReturn evaluateObject(const char *objectName,
	                                OSObject **result = 0,
	                                OSObject **params = 0,
	                                IOItemCount paramCount = 0,
	                                IOOptionBits options = 0);

	/*
	 * Convenience wrapper around evaluateObject() for integer-valued
	 * objects; IOUSBController uses it for "XHCN".
	 */
	virtual IOReturn evaluateInteger(const char *objectName,
	                                 UInt32 *resultInt32,
	                                 OSObject **params = 0,
	                                 IOItemCount paramCount = 0,
	                                 IOOptionBits options = 0);
};

#endif /* _IOKIT_ACPI_IOACPIPLATFORMDEVICE_H_COMPAT */
