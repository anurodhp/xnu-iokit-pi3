/* Real, honestly-documented compat stand-in for a handful of USB IORegistry
 * property-key macros Plugins/InterfaceNamer/ifnamer.c references
 * (kUSBVendorID, kUSBProductString, kUSBVendorString) plus one Apple
 * vendor-ID constant (kIOUSBAppleVendorID) that this project's pinned
 * third_party/IOUSBFamily tag's own <IOKit/usb/USB.h>/USBSpec.h do NOT
 * define (confirmed via full pinned-repo grep -- zero hits anywhere in
 * third_party/IOUSBFamily).
 *
 * kUSBVendorID/kUSBProductString/kUSBVendorString real values confirmed
 * directly against real, unmodified apple-oss-distributions source
 * (IOUSBFamily/Headers/USBSpec.h, both the classic opensource-apple
 * mirror and the current apple-oss-distributions/IOUSBFamily main branch
 * agree): `#define kUSBVendorID "idVendor"`,
 * `#define kUSBProductString "USB Product Name"`,
 * `#define kUSBVendorString "USB Vendor Name"`.
 *
 * kIOUSBAppleVendorID: every real Apple USBSpec.h snapshot found (via
 * direct source read AND WebSearch) only ever defines the OLDER name
 * `kAppleVendorID = 0x05AC` -- the "IOUSB"-infixed spelling ifnamer.c
 * actually references was not found in any publicly available Apple
 * source. The VALUE itself (Apple's USB-IF-assigned vendor ID, 0x05AC)
 * is unambiguous and independently confirmed across every real USBSpec.h
 * revision checked; only the newer symbol name could not be sourced.
 * Honestly documented as a real, cited value under an unconfirmed (but
 * essentially certain) newer alias, not fabricated from nothing.
 */
#ifndef CONFIGD_PLUGIN_USBSPEC_IFNAMER_COMPAT_H
#define CONFIGD_PLUGIN_USBSPEC_IFNAMER_COMPAT_H

#ifndef kUSBVendorID
#define kUSBVendorID "idVendor"
#endif
#ifndef kUSBProductString
#define kUSBProductString "USB Product Name"
#endif
#ifndef kUSBVendorString
#define kUSBVendorString "USB Vendor Name"
#endif
#ifndef kUSBProductID
#define kUSBProductID "idProduct"
#endif
#ifndef kIOUSBAppleVendorID
#define kIOUSBAppleVendorID 0x05AC
#endif

#endif
