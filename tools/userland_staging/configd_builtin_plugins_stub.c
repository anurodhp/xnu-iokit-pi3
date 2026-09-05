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
 * outstanding at that point in the link get pulled in.
 *
 * DAR-240 (same day, follow-up) closed wall #1 for real, mostly: the real
 * SCNetworkConfiguration/SCNetworkSet/SCNetworkService/SCBridgeInterface/
 * SCVLANInterface family InterfaceNamer/ifnamer.c, LinkConfiguration/
 * linkconfig.c, and PreferencesMonitor/prefsmon.c all genuinely, heavily
 * call into (SCNetworkSetCopyCurrent, SCNetworkServiceCopyAll,
 * _SCNetworkInterfaceCreateCapabilities, _SCNetworkConfigurationUpgrade,
 * etc -- DAR-240's own ~25 real symbols, confirmed at real link time) is
 * now really ported into libSystemConfiguration.dylib
 * (build_libsystemconfiguration_client.sh: SCNetworkConfigurationInternal.c,
 * SCNetworkConfigurationPrivate.c, SCNetworkInterface.c, SCNetworkService.c,
 * SCNetworkSet.c, SCNetworkProtocol.c, SCPreferencesPathKey.c,
 * BridgeConfiguration.c, VLANConfiguration.c, LinkConfiguration.c -- real
 * file-name corrections vs. DAR-240's own filed guesses, see that
 * script's own FILES-list comment). `_SCNetworkConfigurationUpgrade`'s
 * real, load-time-critical call from `load_PreferencesMonitor()` itself
 * is now backed by a real, defined symbol
 * (SCNetworkConfigurationPrivate.c), not deferred.
 *
 * This file's load_LinkConfiguration/load_PreferencesMonitor/
 * prime_PreferencesMonitor stand-ins are REMOVED (below) for the same
 * real reason DAR-241 already removed load_IPMonitor/prime_IPMonitor/
 * load_KernelEventMonitor/prime_KernelEventMonitor: with no earlier-on-
 * the-link-line definition to shadow them, ld64's real archive-member
 * selection now pulls in libconfigd_plugins.a's own real
 * linkconfig.o/prefsmon.o (and their real object-file dependents) to
 * satisfy those 3 symbols instead -- confirmed via a real, clean,
 * 0-undefined-symbol link.
 *
 * load_InterfaceNamer stays a stand-in: DAR-240's real fix unblocked
 * ifnamer.o's own SCNetworkConfiguration-family symbols, but real linking
 * then surfaced a SEPARATE, genuinely distinct real wall -- ifnamer.c
 * also calls the real, high-level IOKitLib.h API (IOServiceGetMatchingService,
 * IORegistryEntryCreateCFProperty, IONotificationPortCreate, etc, ~28
 * symbols) plus a real lockdown.h/haveLockdown() notification family (5
 * symbols) neither of which any of DAR-240's other two plugins reference
 * at all (confirmed: the real link's own undefined-symbol report cited
 * ifnamer.o as the sole referencing object for every one of these). Real,
 * already-known, pre-existing wall: this project's own
 * build_iokituser.sh has documented since its own first pass that
 * IOKitLib.c itself (the real userland IOKitLib.framework implementation
 * these symbols come from) hard-depends on CoreFoundation/libdispatch/
 * libxpc and could not be built at that time; CF/dispatch/xpc dylibs do
 * exist now (this project's later work), but a real, timeboxed retest
 * this session found IOKitLib.c still does NOT compile clean even with
 * them available (hit a real missing-header gap + a real macro-expansion
 * syntax error in dispatch/source_private.h within the first two
 * iterations) -- a real, still-substantial, multi-gap port, not a quick
 * follow-on fix, and NOT part of DAR-240's own SCNetworkConfiguration-
 * family scope. Filed separately (see DAR-240's own Linear comment for
 * the exact new ticket ID) rather than guessed at or worked around here.
 *
 * QoSMarking (load_QoSMarking, no prime) remains a stand-in -- that's
 * real ObjC, DAR-239's scope, untouched.
 *
 * The two remaining stand-ins below are real, minimal, functionally-
 * honest implementations of the exact real SCDynamicStoreBundleLoadFunction
 * signature (SCDPlugin.h) plugin_support.c itself calls -- they log via
 * the real SC_log()/os_log path that they were never really loaded, then
 * return, so configd.tproj's own real bundle bookkeeping (allBundles,
 * exiting, etc) stays exactly as correct as it would be for a real
 * bundle whose load() function does nothing yet.
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
		SC_log(LOG_NOTICE, "[" #name "] real plugin not yet ported (DAR-239/DAR-240 -- builtin stand-in)"); \
	}

NOT_YET_PORTED(QoSMarking)
NOT_YET_PORTED(InterfaceNamer)
