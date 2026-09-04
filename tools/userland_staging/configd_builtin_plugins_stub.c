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
 * which produces libc_build/libconfigd_plugins.a) but found two real,
 * hard architectural walls at the LINK stage.
 *
 * DAR-241 (same day, follow-up) closed wall #2 for real: IPMonitor's
 * ip_plugin.c (and 4 of its plain-C siblings -- proxy-configuration.c,
 * dns-configuration.c, nat64-configuration.c, set-hostname.c; a 6th,
 * smb-configuration.c, is deliberately excluded, real Apple's own
 * `#if !TARGET_OS_IPHONE` gate -- see build_configd_plugins.sh's own
 * comment) now compile+link clean into libconfigd_plugins.a alongside
 * eventmon.c. build_configd.sh now links libconfigd_plugins.a as a
 * whole archive AFTER this file's own object -- real ld64 archive
 * member selection means only the members that resolve a symbol still
 * outstanding at that point in the link get pulled in. Since this file
 * no longer defines load_IPMonitor/prime_IPMonitor/
 * load_KernelEventMonitor/prime_KernelEventMonitor (removed below),
 * those four symbols are still outstanding when the linker reaches
 * libconfigd_plugins.a, so it pulls in the REAL ip_plugin.o/eventmon.o
 * (and their real object-file dependents) to satisfy them. This file's
 * load_InterfaceNamer/load_LinkConfiguration/load_PreferencesMonitor/
 * prime_PreferencesMonitor stand-ins are DELIBERATELY KEPT and still
 * resolve those symbols FIRST (earlier on the link line, via this file's
 * own inclusion in libconfigd.a) -- so libconfigd_plugins.a's own
 * ifnamer.o/linkconfig.o/prefsmon.o members are never pulled in at all,
 * correctly leaving wall #1 (below) exactly as unaddressed as before:
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
 *    SCDynamicStore server down. This is DAR-240's own, separate,
 *    precisely-scoped ticket -- not touched here.
 *
 * QoSMarking (load_QoSMarking, no prime) remains a stand-in too --
 * that's real ObjC, DAR-239's scope, untouched.
 *
 * The four remaining stand-ins below are real, minimal, functionally-
 * honest implementations of the exact real SCDynamicStoreBundleLoadFunction/
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
		SC_log(LOG_NOTICE, "[" #name "] real plugin not yet ported (DAR-170/DAR-239/DAR-240 -- builtin stand-in)"); \
	}

NOT_YET_PORTED(InterfaceNamer)
NOT_YET_PORTED(LinkConfiguration)
NOT_YET_PORTED(PreferencesMonitor)
NOT_YET_PORTED(QoSMarking)

void
prime_PreferencesMonitor(void)
{
	SC_log(LOG_NOTICE, "[PreferencesMonitor] real plugin not yet ported (DAR-170/DAR-240 -- builtin stand-in)");
}
