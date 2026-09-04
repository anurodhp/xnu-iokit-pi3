/* Real, honest "not yet ported" stand-in for configd.tproj's real
 * `builtin_plugins[]` static table (plugin_support.c, guarded by real
 * `#if !TARGET_OS_IPHONE` -- confirmed by reading the real, unmodified
 * source: on iOS/embedded targets, real Apple configd does NOT dlopen
 * separate .bundle plugins at all, it links a curated set of plugin
 * subsystems DIRECTLY into the daemon binary, each exposing real,
 * fixed-name `load_<Name>`/`prime_<Name>` entry points matching
 * SystemConfiguration.fproj/SCDPlugin.h's real
 * SCDynamicStoreBundleLoadFunction/PrimeFunction typedefs.
 *
 * DAR-170 Tier 1 step 3 ported the real SCDynamicStore SERVER ENGINE
 * (configd.tproj's own 21 real Sources-phase .c/.m files).
 *
 * DAR-170's 2026-09-04 post-split session (ticket narrowed to the non-ObjC
 * plugins, IPMonitor/QoSMarking moved to DAR-239) got the real, plain-C
 * InterfaceNamer/LinkConfiguration/KernelEventMonitor/PreferencesMonitor
 * source files themselves compiling clean (see build_configd_plugins.sh,
 * which produces libc_build/libconfigd_plugins.a) -- but found two real,
 * hard architectural walls at the LINK stage that mean these four cannot
 * be safely wired in here yet, and this file's stand-ins for all six
 * plugins are still the correct, honest, currently-linked state:
 *
 * 1. InterfaceNamer/ifnamer.c, LinkConfiguration/linkconfig.c, and
 *    PreferencesMonitor/prefsmon.c all genuinely, heavily call into the
 *    real SCNetworkConfiguration/SCNetworkInterface/SCNetworkSet/
 *    SCBridgeInterface/SCVLANInterface family (SCNetworkSetCopyCurrent,
 *    SCNetworkServiceCopyAll, _SCNetworkInterfaceCreateCapabilities,
 *    _SCNetworkConfigurationUpgrade, etc) -- confirmed at real link time,
 *    not assumed. This is the SAME family build_libsystemconfiguration_
 *    client.sh's own session already deliberately excluded from
 *    libSystemConfiguration.dylib as "a separate, substantial real
 *    subsystem" -- a real port of its own (SCNetworkConfiguration.c,
 *    SCNetworkService.c, SCNetworkSet.c, SCBridgeInterface.c,
 *    SCVLANInterface.c, likely thousands of real lines), not a narrow fix.
 *    Critically, `_SCNetworkConfigurationUpgrade` is called DIRECTLY from
 *    `load_PreferencesMonitor()` itself -- i.e. at plugin load time, not a
 *    rare/optional code path -- so deferring these symbols via `-Wl,-U`
 *    would move the crash from a caught link-time error to a real
 *    dyld-load-time abort of configd.tproj ITSELF the moment
 *    PreferencesMonitor's real load() runs, which would take the whole
 *    SCDynamicStore server down (everything else, including any future
 *    IPConfiguration bundle, depends on that server being up). Filed as
 *    its own real, precisely-scoped Linear issue rather than papered over.
 * 2. KernelEventMonitor/eventmon.c calls DIRECTLY (real C function calls,
 *    not SCDynamicStore) into real functions that only exist in
 *    IPMonitor's own ip_plugin.c (interfaceListAddInterface/Copy/Update,
 *    interface_update_ipv6/idle_state/link_issues/quality_metric,
 *    ipv4_interface_update/arp_collision/router_arp_alive/failure,
 *    ipv6_duplicated_address/router_expired, link_add/remove/
 *    update_status, nat64_prefix_request) -- confirmed at real link time.
 *    This is a real, new architectural finding: real Apple's six plugins
 *    are NOT cleanly separable via SCDynamicStore alone -- some are
 *    directly, statically coupled by C function call, which is exactly
 *    why real Apple links all six into one binary in the first place.
 *    These functions are reached from `_processEvent_Apple_Network` and
 *    `_update_interfaces`, i.e. on real, ordinary kernel network events
 *    (link up/down, address changes) -- not a rare/optional path, so
 *    again not safely `-Wl,-U`-deferrable. Also filed as its own Linear
 *    issue, cross-referencing DAR-239 since it also concerns IPMonitor's
 *    real scope/shape (ip_plugin.c itself is real plain C, unlike
 *    IPMonitor's five real ObjC "Agent" files -- a nuance DAR-239 should
 *    know about).
 *
 * The six stand-ins below are real, minimal, functionally-honest
 * implementations of the exact real SCDynamicStoreBundleLoadFunction/
 * SCDynamicStoreBundlePrimeFunction signatures (SCDPlugin.h) plugin_support.c
 * itself calls -- they log via the real SC_log()/os_log path that they were
 * never really loaded, then return, so configd.tproj's own real bundle
 * bookkeeping (allBundles, exiting, etc) stays exactly as correct as it
 * would be for a real bundle whose load() function does nothing yet.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SCDPlugin.h>
#include <SystemConfiguration/SCPrivate.h>

#define NOT_YET_PORTED(name) \
	void \
	load_##name(CFBundleRef bundle, Boolean bundleVerbose) \
	{ \
		(void)bundle; \
		(void)bundleVerbose; \
		SC_log(LOG_NOTICE, "[" #name "] real plugin not yet ported (DAR-170/DAR-239 -- builtin stand-in)"); \
	}

NOT_YET_PORTED(IPMonitor)
NOT_YET_PORTED(InterfaceNamer)
NOT_YET_PORTED(KernelEventMonitor)
NOT_YET_PORTED(LinkConfiguration)
NOT_YET_PORTED(PreferencesMonitor)
NOT_YET_PORTED(QoSMarking)

void
prime_IPMonitor(void)
{
	SC_log(LOG_NOTICE, "[IPMonitor] real plugin not yet ported (DAR-170/DAR-239 -- builtin stand-in)");
}

void
prime_KernelEventMonitor(void)
{
	SC_log(LOG_NOTICE, "[KernelEventMonitor] real plugin not yet ported (DAR-170/DAR-239 -- builtin stand-in)");
}

void
prime_PreferencesMonitor(void)
{
	SC_log(LOG_NOTICE, "[PreferencesMonitor] real plugin not yet ported (DAR-170/DAR-239 -- builtin stand-in)");
}
