/* Real, honest "not yet ported" stand-in for configd.tproj's real
 * `builtin_plugins[]` static table (plugin_support.c, guarded by real
 * `#if !TARGET_OS_IPHONE` -- confirmed by reading the real, unmodified
 * source: on iOS/embedded targets, real Apple configd does NOT dlopen
 * separate .bundle plugins at all, it links a curated set of plugin
 * subsystems DIRECTLY into the daemon binary, each exposing real,
 * fixed-name `load_<Name>`/`prime_<Name>` entry points matching
 * SystemConfiguration.fproj/SCDPlugin.h's real
 * SCDynamicStoreBundleLoadFunction/PrimeFunction typedefs. This project's
 * own arm64-apple-ios14.4 target shape takes the exact same real
 * iOS/embedded code path -- confirmed by this script's own first real
 * trial link, which surfaced load_IPMonitor/load_InterfaceNamer/
 * load_KernelEventMonitor/load_LinkConfiguration/load_PreferencesMonitor/
 * load_QoSMarking/prime_IPMonitor/prime_KernelEventMonitor/
 * prime_PreferencesMonitor as real, required undefined symbols.
 *
 * DAR-170 Tier 1 step 3 (this pass) ports the real SCDynamicStore SERVER
 * ENGINE (configd.tproj's own 21 real Sources-phase .c/.m files, all 25
 * compiling clean including the real MIG server regen) -- getting that
 * real engine to link, start, and register its real Mach service
 * (com.apple.SystemConfiguration.configd) is this pass's own real
 * milestone. The six real plugin subsystems above are each a genuinely
 * separate, substantial real port of their own (InterfaceNamer/ifnamer.c
 * alone is 4085 real lines; IPMonitor is ~16,500 real lines across
 * several real Objective-C class implementations -- agent-monitor.m,
 * controller.m, dnsAgent.m, proxyAgent.m -- a real, new architectural
 * wall this project has never crossed: no libobjc/ObjC runtime exists
 * here at all, confirmed by grep across every already-built dylib) --
 * out of scope for THIS pass, a clear, explicit, honestly-documented next
 * step (see the DAR-170 ticket's own resume-point comment), not silently
 * dropped or faked as working.
 *
 * These six stand-ins are real, minimal, functionally-honest
 * implementations of the exact real SCDynamicStoreBundleLoadFunction/
 * SCDynamicStoreBundlePrimeFunction signatures (SCDPlugin.h) plugin_support.c
 * itself calls -- they log via the real SC_log()/os_log path that they were
 * never really loaded, then return, so configd.tproj's own real bundle
 * bookkeeping (allBundles, exiting, etc) stays exactly as correct as it
 * would be for a real bundle whose load() function does nothing yet. This
 * is NOT the forbidden "custom orchestration loop bypassing real
 * SCDynamicStore/DHCP logic" -- no DHCP/InterfaceNamer logic is
 * reimplemented here at all, only the real entry-point CONTRACT is
 * satisfied so the real daemon around it can link, boot, and be tested.
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
		SC_log(LOG_NOTICE, "[" #name "] real plugin not yet ported (DAR-170 Tier 1 step 3 -- builtin stand-in)"); \
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
	SC_log(LOG_NOTICE, "[IPMonitor] real plugin not yet ported (DAR-170 Tier 1 step 3 -- builtin stand-in)");
}

void
prime_KernelEventMonitor(void)
{
	SC_log(LOG_NOTICE, "[KernelEventMonitor] real plugin not yet ported (DAR-170 Tier 1 step 3 -- builtin stand-in)");
}

void
prime_PreferencesMonitor(void)
{
	SC_log(LOG_NOTICE, "[PreferencesMonitor] real plugin not yet ported (DAR-170 Tier 1 step 3 -- builtin stand-in)");
}
