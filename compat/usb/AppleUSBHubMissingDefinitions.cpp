/*
 * AppleUSBHubMissingDefinitions.cpp -- two AppleUSBHub member functions
 * that the published source declares and calls but never defines.
 *
 * Vendored into third_party/xnu-7195/iokit/Drivers/AppleUSBHub/ by
 * tools/vendor_usb_into_xnu.sh (DAR-353).
 *
 * Found at the kernel link, not by reading:
 *
 *   Undefined symbols for architecture arm64:
 *     "AppleUSBHub::SetPortPower(unsigned short, unsigned int)"
 *     "AppleUSBHubPort::HandleLinkState(unsigned short, unsigned short)"
 *
 * Neither has a definition in ANY published IOUSBFamily release. Checked
 * with `git grep` over the 560.4.2 pin and the 555.4.4, 540.4.1, 516.4.1,
 * 458.4.1 and 440.4.1 tags from apple-oss-distributions/IOUSBFamily
 * (630.4.5 publishes zero-byte sources). Every one of them declares
 * SetPortPower in AppleUSBHub.h and calls it; none defines it. Apple
 * shipped these from a file it did not publish, the same class of gap as
 * IOUSBControllerV3::GetErrataBits (tools/vendor_usb_into_xnu.sh FIX 1c).
 */

#include <IOKit/usb/USBHub.h>
#include "AppleUSBHub.h"
#include "AppleUSBHubPort.h"

/*
 * Port power is the hub-class PORT_POWER feature (USB 2.0 11.24.2.7.1.6):
 * SET_FEATURE turns it on, CLEAR_FEATURE turns it off. That is the only
 * reading the callers allow. AppleUSBHubPort::stop()
 * (third_party/IOUSBFamily/AppleUSBHub/Classes/AppleUSBHubPort.cpp:479-482)
 * reports a failed SetPortPower(_portNum, kHubPortPowerOff) as
 * "err ... from ClearPortFeature(kUSBHubPortPowerFeature)", and
 * DetachDevice()'s attach-retry path (:3697, :3713) power-cycles the port
 * with the Off/On pair, reporting failures as "clearing port power
 * feature" / "setting port power feature". kHubPortPowerOff/On are 0/1
 * (AppleUSBHub.h:58-59).
 */
IOReturn
AppleUSBHub::SetPortPower(UInt16 port, UInt32 on)
{
	if (on == kHubPortPowerOn) {
		return SetPortFeature(kUSBHubPortPowerFeature, port);
	}
	return ClearPortFeature(kUSBHubPortPowerFeature, port);
}

/*
 * SuperSpeed only. The one caller, DefaultPortLinkStateChangeHandler()
 * (AppleUSBHubPort.cpp:3152-3165), is installed as the handler for the
 * kSSHubPortChangePortLinkStateMask change bit (:3570-3572;
 * USBHub.h:123), a USB 3.0 hub port-change bit. On a USB 2.0 hub that bit
 * is reserved and reads 0 (USB 2.0 Table 11-22), and this board has no
 * USB 3.0 hardware (DWC2 is a USB 2.0 OTG core), so it cannot be reached.
 * Report that honestly rather than inventing link-state handling Apple
 * never published.
 */
IOReturn
AppleUSBHubPort::HandleLinkState(UInt16 changeFlags, UInt16 statusFlags)
{
	IOLog("AppleUSBHubPort: unexpected SuperSpeed link-state change on port %d "
	    "(change 0x%04x status 0x%04x) - not implemented on this USB 2.0 "
	    "controller\n", _portNum, changeFlags, statusFlags);
	return kIOReturnUnsupported;
}
