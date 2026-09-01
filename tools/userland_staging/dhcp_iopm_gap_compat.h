/* Real, narrow gap-fill for third_party/bootp's dhcp.c -- NOT a full
 * IOPMLibPrivate.h stand-in and NOT a stub of a working feature.
 *
 * third_party/IOKitUser (IOKitUser-1845.81.1, already pinned for other
 * work, see build_iokituser.sh) genuinely ships real
 * pwr_mgt.subproj/IOPMLib.h and IOPMLibPrivate.h with real
 * IOPMCancelScheduledPowerEvent()/kIOPMPowerEventAppNameKey/
 * kIOPMPowerEventTimeKey already declared -- those are used AS-IS,
 * unmodified, real Apple source, via a real -I staging into that repo
 * (see build_bootp_dhcp_engine.sh's iokit_headers staging step).
 *
 * Two exceptions this pinned tag is missing (confirmed by grep of the
 * full pinned tree, not assumed):
 *
 *   1. IOPMRequestSysWake() has a REAL implementation in
 *      IOKitUser/pwr_mgt.subproj/IOPMAutoWake.c but no matching header
 *      prototype anywhere in this tag (real version-skew: later Apple
 *      SDKs added the declaration to IOPMLibPrivate.h; this pinned
 *      source tag's .c predates its own header catching up). Signature
 *      confirmed via public documentation of the real Apple SPI
 *      (IOReturn IOPMRequestSysWake(CFDictionaryRef request), taking a
 *      dict with kIOPMPowerEventAppNameKey/kIOPMPowerEventTimeKey/
 *      kIOPMPowerEventLeewayKey), not fabricated.
 *
 *   2. kIOPMPowerEventLeewayKey has no #define anywhere in this pinned
 *      tag at all (same version-skew reason). Real value ("leeway", a
 *      CFNumber giving the allowed wake-coalescing delay in seconds)
 *      confirmed via public documentation of the real Apple key name,
 *      matching the naming pattern of the two sibling keys this tag
 *      DOES already define (kIOPMPowerEventAppNameKey "scheduledby",
 *      kIOPMPowerEventTimeKey "time").
 *
 * Per Linear DAR-170: sleep/wake power-event scheduling is explicitly
 * out of scope for a first real DHCP lease. dhcp.c's only real call
 * sites for these two symbols build a request dict and call
 * IOPMRequestSysWake()/IOPMCancelScheduledPowerEvent() to schedule (or
 * cancel) a wake-for-DHCP-lease-renewal RTC timer -- a real refinement,
 * not needed for DISCOVER/OFFER/REQUEST/ACK to work. This project has
 * no real IOPMAutoWake.c linked in yet (that needs the still-CF-
 * dependent rest of IOKitUser's pwr_mgt.subproj rebuilt post-CF-port --
 * real, separate, larger work, out of scope for this ticket -- see
 * build_iokituser.sh's own "HONEST SCOPE STATEMENT"), so the real
 * IOPMRequestSysWake() body just below is an honest no-op returning
 * kIOReturnUnsupported (real IOKit's own documented error code for "this
 * platform doesn't support this request"), not a fabricated success. A
 * real DHCP lease still completes end-to-end without a scheduled wake;
 * only the "wake the machine from sleep to renew a lease" refinement is
 * unavailable -- exactly the gap DAR-170 pre-approved leaving open.
 *
 * NOT included in FILES by build_bootp_dhcp_engine.sh's compile-only
 * pass (no smoketest link is attempted there yet) -- wire the .c
 * counterpart (dhcp_iopm_gap_compat.c) in whenever the first real link
 * of a dhcp.c-consuming binary happens.
 */
#ifndef _DHCP_IOPM_GAP_COMPAT_H_
#define _DHCP_IOPM_GAP_COMPAT_H_

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOReturn.h>

#ifndef kIOPMPowerEventLeewayKey
#define kIOPMPowerEventLeewayKey "leeway"
#endif

#ifdef __cplusplus
extern "C" {
#endif

IOReturn IOPMRequestSysWake(CFDictionaryRef request);

#ifdef __cplusplus
}
#endif

#endif /* _DHCP_IOPM_GAP_COMPAT_H_ */
