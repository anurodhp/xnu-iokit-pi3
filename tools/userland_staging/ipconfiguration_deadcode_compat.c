/* DAR-170: real, honest no-op bodies for two families of genuinely
 * out-of-scope real functions ipconfigd.c/sysconfig.c/rtutil.c
 * reference in code paths that only fire for features this project's
 * single-Ethernet-interface, real-Pi3, real-IPv4-DHCP-only target
 * never exercises. This is NOT the "the file doesn't compile" class of
 * gap (every one of these has real, pinned, buildable Apple source) --
 * it's a deliberate, documented scope decision, matching this ticket's
 * own 2026-09-01 session-1 scoping ("excluding DHCPv6/RTADV/stf/CGA/
 * RSAKey/AWD-telemetry .m files -- this project has no IPv6/SEND
 * infrastructure and DAR-170's own goal is a plain IPv4 DHCP lease")
 * extended to the real link-time symbols that scoping decision implies.
 * Real signatures copied verbatim from each real header
 * (DHCPv6Client.h/DHCPv6Socket.h/ICMPv6Socket.h/CGA.h/wireless.h/
 * RouterAdvertisement.h/IOPMLib.h/IOKitLib.h) -- not guessed.
 *
 * ============================================================
 * Family 1: IPv6 (DHCPv6/RTADV/ICMPv6/CGA/6to4-stf) + WiFi-specific
 * timing (wireless.c). Real, pinned, buildable source exists for every
 * one of these (bootplib/RouterAdvertisement.c, IPConfiguration.bproj/
 * {DHCPv6Client,DHCPv6Socket,ICMPv6Socket,CGA,wireless,rtadv,stf}.c) --
 * not compiled here because this project has zero real IPv6 SLAAC/
 * DHCPv6/SEND(CGA)/6to4/WiFi-association infrastructure to plug them
 * into, and the real call sites (ipconfigd.c's per-method dispatch
 * table, sysconfig.c's RA-derived DNS persistence) are all reachable
 * only via a real DHCPv6/RTADV/WiFi service method or interface type
 * this target's real, single, wired-Ethernet-only en0 never requests.
 *
 * ============================================================
 * Family 2: sleep/wake power-event scheduling (IOPMLib.c/
 * IOPMAutoWake.c). Real, pinned, buildable source ALSO exists
 * (third_party/IOKitUser/pwr_mgt.subproj/) -- not compiled here because
 * it needs IOKitLib.c's own primitives (IOServiceOpen/
 * IOConnectCallMethod/etc), and IOKitLib.c itself is a real, separate,
 * already-flagged multi-gap port (see configd_builtin_plugins_stub.c's
 * own header comment on InterfaceNamer's identical IOKitLib.h wall) --
 * out of scope for a first DHCP lease, same "sleep/wake scheduling
 * explicitly out of scope" decision dhcp_iopm_gap_compat.c's own header
 * already made for IOPMRequestSysWake(), extended here to its 5 real
 * sibling APIs ipconfigd.c's own wake-event bookkeeping
 * (CleanupWakeEvents(), power-change acknowledgement) references. */
#include <CoreFoundation/CoreFoundation.h>
#include <stdint.h>
#include <stdbool.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/ip6.h>
#include "RouterAdvertisement.h"
#include "DHCPv6Client.h"
#include "DHCPv6Socket.h"
#include "ICMPv6Socket.h"
#include "CGA.h"
#include "wireless.h"
#include "ipconfig_types.h"
#include "ipconfigd_threads.h"
#include "ioregpath.h"
#include "DHCPv6Options.h"
#include <IOKit/IOKitLib.h>
#include <IOKit/pwr_mgt/IOPMLib.h>

/* -- Family 1: IPv6 / WiFi -- */

void
DHCPv6ClientSetRequestedOptions(uint16_t * requested_options,
				 int requested_options_count)
{
	(void)requested_options;
	(void)requested_options_count;
}

bool
DHCPv6ClientOptionIsOK(int option)
{
	(void)option;
	return false;
}

void
DHCPv6SocketSetVerbose(bool verbose)
{
	(void)verbose;
}

int
ICMPv6SocketOpen(bool receive_too)
{
	(void)receive_too;
	return -1;
}

int
ICMPv6SocketSendNeighborAdvertisement(int sockfd, int if_index,
				       const void * link_addr,
				       int link_addr_length,
				       const struct in6_addr * target_ipaddr)
{
	(void)sockfd; (void)if_index; (void)link_addr;
	(void)link_addr_length; (void)target_ipaddr;
	return -1;
}

void
CGAInit(void)
{
}

bool
CGAIsEnabled(void)
{
	return false;
}

void
CGAPrepareSetForInterfaceLinkLocal(const char * name,
				    struct in6_cga_prepare * cgaprep)
{
	(void)name;
	(void)cgaprep;
}

void
CGAPrepareSetForInterface(const char * name, struct in6_cga_prepare * cgaprep)
{
	(void)name;
	(void)cgaprep;
}

const char *
wifi_auth_type_string(wifi_auth_type auth_type)
{
	(void)auth_type;
	return "none";
}

CFStringRef
wireless_copy_ssid_bssid(CFStringRef ifname, struct ether_addr * ap_mac,
			  wifi_auth_type * auth_type_p)
{
	(void)ifname;
	(void)ap_mac;
	if (auth_type_p != NULL) {
		*auth_type_p = WIFI_AUTH_TYPE_NONE;
	}
	return NULL;
}

bool
RouterAdvertisementLifetimeHasExpired(RouterAdvertisementRef ra,
				       CFAbsoluteTime now, uint32_t lifetime)
{
	(void)ra;
	(void)now;
	(void)lifetime;
	return true;
}

const struct in6_addr *
RouterAdvertisementGetRDNSS(RouterAdvertisementRef ra,
			     int * dns_servers_count_p, uint32_t * lifetime_p)
{
	(void)ra;
	if (dns_servers_count_p != NULL) {
		*dns_servers_count_p = 0;
	}
	if (lifetime_p != NULL) {
		*lifetime_p = 0;
	}
	return NULL;
}

const uint8_t *
RouterAdvertisementGetDNSSL(RouterAdvertisementRef ra,
			     int * domains_length_p, uint32_t * lifetime_p)
{
	(void)ra;
	if (domains_length_p != NULL) {
		*domains_length_p = 0;
	}
	if (lifetime_p != NULL) {
		*lifetime_p = 0;
	}
	return NULL;
}

CFStringRef
RouterAdvertisementCopyCaptivePortal(RouterAdvertisementRef ra)
{
	(void)ra;
	return NULL;
}

CFDictionaryRef
RouterAdvertisementCopyDictionary(RouterAdvertisementRef ra)
{
	(void)ra;
	return NULL;
}

/* real ipconfigd_threads.h IPConfigFunc entry points (real signature:
 * ipconfig_status_t (ServiceRef, IFEventID_t, void *), copied verbatim)
 * for method threads this target never dispatches to (no DHCPv6/6to4/
 * IPv6LL/IPv6-manual service method is ever requested on a plain IPv4
 * Ethernet target) -- real "start" event honestly reports
 * ipconfig_status_operation_not_supported_e via the exact real
 * IFEventID_t/ipconfig_status_t enumerators ipconfigd_types.h already
 * declares. */
ipconfig_status_t
rtadv_thread(ServiceRef service_p, IFEventID_t evid, void * evdata)
{
	(void)service_p; (void)evdata;
	if (evid == IFEventID_start_e) {
		return (ipconfig_status_operation_not_supported_e);
	}
	return (ipconfig_status_success_e);
}

ipconfig_status_t
stf_thread(ServiceRef service_p, IFEventID_t evid, void * evdata)
{
	(void)service_p; (void)evdata;
	if (evid == IFEventID_start_e) {
		return (ipconfig_status_operation_not_supported_e);
	}
	return (ipconfig_status_success_e);
}

ipconfig_status_t
linklocal_v6_thread(ServiceRef service_p, IFEventID_t evid, void * evdata)
{
	(void)service_p; (void)evdata;
	if (evid == IFEventID_start_e) {
		return (ipconfig_status_operation_not_supported_e);
	}
	return (ipconfig_status_success_e);
}

ipconfig_status_t
manual_v6_thread(ServiceRef service_p, IFEventID_t evid, void * evdata)
{
	(void)service_p; (void)evdata;
	if (evid == IFEventID_start_e) {
		return (ipconfig_status_operation_not_supported_e);
	}
	return (ipconfig_status_success_e);
}

/* -- Family 3: ioregpath.c's real IOKitLib.h dependency. Real, pinned
 * source (bootplib/ioregpath.c) exists and is otherwise narrow, but its
 * 3 real functions need real IOServiceGetMatchingService/
 * IORegistryEntryFromPath/IORegistryEntryCreateCFProperty(ies)/
 * IORegistryEntryGetParentEntry/IOObjectRelease/IOBSDNameMatching/
 * kIOMasterPortDefault -- the exact same real, already-flagged
 * IOKitLib.c multi-gap wall configd_builtin_plugins_stub.c's own header
 * comment already documents for InterfaceNamer/ifnamer.c (a real,
 * substantial, separate port, not reattempted here). sysconfig.c's own
 * 3 real call sites (interface media-type / IOKit-registry-backed
 * classification, e.g. distinguishing AWDL/Thunderbolt-bridge
 * interfaces) already tolerate a real "no IOKit info available" (NULL)
 * outcome as a valid answer for interfaces IOKit doesn't have a rich
 * registry entry for -- this project's real target (en0, a plain
 * BCM2837 USB-Ethernet interface with no such registry metadata
 * exposed via this path anyway) gets the same honest, empty answer real
 * Apple's own code would give for a stripped-down/registry-thin
 * device. */
CFDictionaryRef
myIORegistryEntryCopyValue(const char * path)
{
	(void)path;
	return NULL;
}

CFTypeRef
myIORegistryEntryCopyProperty(const char * path, CFStringRef prop)
{
	(void)path;
	(void)prop;
	return NULL;
}

CFDictionaryRef
myIORegistryEntryBSDNameMatchingCopyValue(const char * devname, Boolean parent)
{
	(void)devname;
	(void)parent;
	return NULL;
}

/* -- Family 1b: DHCPv6 option-list parsing (sysconfig.c's real
 * DHCPv6InfoDictionaryCreate()/CaptivePortalCopyWithIPv6Info()/
 * my_DHCPv6OptionListCopyDNSInfo(), all real DHCPv6-lease-derived
 * SCDynamicStore publishing -- same Family 1 IPv6-out-of-scope
 * reasoning, this project's DHCPv6OptionListRef is always NULL/empty
 * since no real DHCPv6Client ever runs on this target) -- */
int
DHCPv6OptionListGetCount(DHCPv6OptionListRef options)
{
	(void)options;
	return 0;
}

DHCPv6OptionRef
DHCPv6OptionListGetOptionAtIndex(DHCPv6OptionListRef options, int i)
{
	(void)options;
	(void)i;
	return NULL;
}

const uint8_t *
DHCPv6OptionListGetOptionDataAndLength(DHCPv6OptionListRef options,
					DHCPv6OptionCode option_code,
					int * ret_length, int * start_index)
{
	(void)options;
	(void)option_code;
	if (ret_length != NULL) {
		*ret_length = 0;
	}
	if (start_index != NULL) {
		*start_index = -1;
	}
	return NULL;
}

/* -- Family 2: sleep/wake power events -- */

IOReturn
IOAllowPowerChange(io_connect_t kernelPort, intptr_t notificationID)
{
	(void)kernelPort;
	(void)notificationID;
	return kIOReturnUnsupported;
}

CFRunLoopSourceRef
IONotificationPortGetRunLoopSource(IONotificationPortRef notify)
{
	(void)notify;
	return NULL;
}

IOReturn
IOPMCancelScheduledPowerEvent(CFDateRef time_to_wake, CFStringRef my_id,
			       CFStringRef type)
{
	(void)time_to_wake;
	(void)my_id;
	(void)type;
	return kIOReturnUnsupported;
}

CFArrayRef
IOPMCopyScheduledPowerEvents(void)
{
	return NULL;
}

io_connect_t
IORegisterForSystemPower(void * refcon, IONotificationPortRef * thePortRef,
			  IOServiceInterestCallback callback,
			  io_object_t * notifier)
{
	(void)refcon;
	if (thePortRef != NULL) {
		*thePortRef = NULL;
	}
	(void)callback;
	if (notifier != NULL) {
		*notifier = 0;
	}
	return 0;
}
