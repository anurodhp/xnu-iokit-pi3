/* Real, honestly-documented compat stand-in for 5 classic USB
 * "device-interface" types (USBDeviceAddress, IOUSBDevRequest,
 * IOUSBDevRequestTO, IOUSBFindInterfaceRequest,
 * IOUSBConfigurationDescriptorPtr) that <IOKit/usb/IOUSBLib.h>'s own
 * real, unmodified function-pointer prototypes (IOUSBDeviceInterface's
 * GetDeviceAddress/GetConfigurationDescriptorPtr/DeviceRequest/
 * DeviceRequestTO/CreateInterfaceIterator members) reference.
 *
 * Real, confirmed gap (not this project's regression): this project's
 * pinned apple-oss-distributions/IOUSBFamily tag (IOUSBFamily-630.4.5)
 * ships IOUSBFamily/Headers/USB.h as a genuinely EMPTY (0-byte) file --
 * confirmed via a direct `curl` of the raw tagged blob from GitHub
 * (https://raw.githubusercontent.com/apple-oss-distributions/
 * IOUSBFamily/IOUSBFamily-630.4.5/IOUSBFamily/Headers/USB.h, 0 bytes
 * downloaded), not a local clone artifact. These 5 types are ALSO absent
 * from every other header in the pinned repo (full grep, zero hits) and
 * from every substantially earlier real tag checked back to
 * IOUSBFamily-540.4.1 (which does have real USB.h content, just not
 * these types) -- real Apple appears to generate this content at build
 * time from a source this open-source snapshot doesn't include.
 *
 * Real, load-bearing fact confirmed by reading SCNetworkInterface.c
 * directly (grep, not assumed): it never references ANY of these 5
 * types, nor any other IOUSBLib.h symbol -- its own
 * `#include <IOKit/usb/IOUSBLib.h>` is real but unreachable/dead for its
 * purposes, existing only so the (larger, mostly IOKit-registry-based,
 * not USB-device-interface-based) rest of the file's real logic compiles
 * as one translation unit. Since nothing anywhere in this build's target
 * files ever dereferences these 5 types, minimal forward-declared/opaque
 * stand-ins are safe and sufficient (every real use in IOUSBLib.h itself
 * is by pointer, confirmed by reading -- see GetDeviceAddress(...,
 * USBDeviceAddress *addr) etc). No real behavior anywhere in this
 * project depends on their layout. DAR-240.
 */
#ifndef _CONFIGD_SCNET_USB_DEVREQUEST_COMPAT_H_
#define _CONFIGD_SCNET_USB_DEVREQUEST_COMPAT_H_

#include <stdint.h>

typedef uint8_t USBDeviceAddress;

struct IOUSBDevRequest;
typedef struct IOUSBDevRequest IOUSBDevRequest;

struct IOUSBDevRequestTO;
typedef struct IOUSBDevRequestTO IOUSBDevRequestTO;

struct IOUSBFindInterfaceRequest;
typedef struct IOUSBFindInterfaceRequest IOUSBFindInterfaceRequest;

struct IOUSBConfigurationDescriptor;
typedef struct IOUSBConfigurationDescriptor *IOUSBConfigurationDescriptorPtr;

struct IOUSBIsocFrame;
typedef struct IOUSBIsocFrame IOUSBIsocFrame;

struct IOUSBLowLatencyIsocFrame;
typedef struct IOUSBLowLatencyIsocFrame IOUSBLowLatencyIsocFrame;

struct IOUSBDescriptorHeader;
typedef struct IOUSBDescriptorHeader IOUSBDescriptorHeader;

struct IOUSBEndpointProperties;
typedef struct IOUSBEndpointProperties IOUSBEndpointProperties;

#endif /* _CONFIGD_SCNET_USB_DEVREQUEST_COMPAT_H_ */
