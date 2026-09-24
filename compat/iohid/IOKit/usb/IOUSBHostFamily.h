/*
 * compat/iohid/IOKit/usb/IOUSBHostFamily.h -- DAR-245.
 *
 * IOHIDFamily-1633 includes <IOKit/usb/IOUSBHostFamily.h> (IOHIDEventService.cpp:30)
 * for exactly one identifier: kUSBHostVendorIDAppleComputer, compared
 * against a device's vendor ID at IOHIDEventService.cpp:442 and
 * IOHIDKeyboard.cpp:465.
 *
 * IOUSBHostFamily is the Big Sur-era USB stack. It was never open-sourced
 * and this board does not run it: the kernel uses the legacy
 * IOUSBFamily-560.4.2, see tools/build_iousbfamily.sh. So this header supplies
 * only that one constant, with the value from Apple's shipped header
 * (Xcode 12 MacOSX.sdk Kernel.framework/Headers/IOKit/usb/
 * IOUSBHostFamily.h:180: `kUSBHostVendorIDAppleComputer = 0x05AC`).
 * 0x05AC is also Apple's USB-IF vendor ID, the same 1452 that
 * IOHIDDevice.cpp:1370 hardcodes in AppleVendorIDs[].
 *
 * Deliberately NOT a copy of the real header, which pulls in the whole
 * IOUSBHost class hierarchy. The include is angle-bracketed and reached
 * only through the per-object -I that tools/iohid_gap_audit.sh and the
 * vendoring step add, so nothing else in the kernel can see it.
 */
#ifndef _COMPAT_IOHID_IOUSBHOSTFAMILY_H
#define _COMPAT_IOHID_IOUSBHOSTFAMILY_H

enum {
	kUSBHostVendorIDAppleComputer = 0x05AC
};

#endif
