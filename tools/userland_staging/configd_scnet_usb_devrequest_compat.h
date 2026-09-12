/* Forward declaration for the ONE classic USB "device-interface" type
 * that <IOKit/usb/IOUSBLib.h>'s real, unmodified function-pointer
 * prototypes reference and that this project's pinned IOUSBFamily
 * headers genuinely do not define: IOUSBEndpointProperties, used by
 * IOUSBInterfaceInterface's GetPipePropertiesV3/GetEndpointPropertiesV3
 * members (and, identically, by IOUSBInterface.h's own
 * GetEndpointPropertiesV3 declaration).
 *
 * HISTORY -- THIS FILE USED TO DECLARE NINE TYPES, AND THAT BROKE (DAR-287)
 * =======================================================================
 * Written for DAR-240, when this project pinned IOUSBFamily-630.4.5,
 * whose IOUSBFamily/Headers/USB.h really is a 0-byte file in the real
 * upstream repo (confirmed then by a direct `curl` of the raw tagged
 * blob, not a local clone artifact). With USB.h empty, nine types
 * IOUSBLib.h references had no definition anywhere, so this header
 * forward-declared all nine.
 *
 * DAR-244 (2026-09-08, commit 8361d3d) re-pinned IOUSBFamily to
 * IOUSBFamily-560.4.2 -- the next-newest tag, chosen precisely because
 * its USB.h is NOT empty -- and nothing updated this header to match.
 * Eight of the nine are now real, unmodified Apple definitions in that
 * checkout (USB.h:206 USBDeviceAddress, :594
 * IOUSBConfigurationDescriptorPtr, :900 IOUSBDevRequest, :928
 * IOUSBDevRequestTO, :1075 IOUSBFindInterfaceRequest, :221
 * IOUSBIsocFrame, :239 IOUSBLowLatencyIsocFrame, :498
 * IOUSBDescriptorHeader), and one of them actively CONFLICTED: this file
 * declared `typedef uint8_t USBDeviceAddress` (a guess, safe only while
 * nothing defined it) against real Apple's `typedef UInt16
 * USBDeviceAddress`, a hard clang error.
 *
 * The damage was silent and real: since this header is force-included
 * (-include) by build_libsystemconfiguration_client.sh,
 * SCNetworkInterface.c stopped compiling, which means
 * libSystemConfiguration.dylib -- and therefore configd.macho /
 * ipconfig.macho / scutil.macho -- could not be rebuilt from source at
 * all. Nothing surfaced it because the already-built dylib kept working;
 * it only appeared the next time someone actually re-ran the script
 * (DAR-287, 2026-09-11), and a `git stash` re-run proved the breakage
 * predated that ticket's own change rather than being caused by it.
 *
 * So: the eight redundant declarations are gone, and the real Apple
 * definitions are used instead -- strictly more real Apple source, and
 * no possibility of this file's guesses disagreeing with them again.
 * IOUSBEndpointProperties stays because it is genuinely absent from
 * every header in the pinned checkout (full grep: it appears only in
 * IOUSBLib.h's and IOUSBInterface.h's own prototypes and doc comments,
 * never in a definition). It is referenced only by pointer, and nothing
 * in any file this project compiles ever dereferences it -- confirmed by
 * grep, and unchanged from DAR-240's own finding that
 * SCNetworkInterface.c references no IOUSBLib.h symbol whatsoever; its
 * `#include <IOKit/usb/IOUSBLib.h>` only has to parse. So an opaque
 * forward declaration is sufficient and no layout is invented.
 */
#ifndef _CONFIGD_SCNET_USB_DEVREQUEST_COMPAT_H_
#define _CONFIGD_SCNET_USB_DEVREQUEST_COMPAT_H_

struct IOUSBEndpointProperties;
typedef struct IOUSBEndpointProperties IOUSBEndpointProperties;

#endif /* _CONFIGD_SCNET_USB_DEVREQUEST_COMPAT_H_ */
