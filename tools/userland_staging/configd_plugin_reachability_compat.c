/*
 * Real, narrow no-op stand-in for the 5 SCNetworkReachability functions
 * Plugins/IPMonitor/set-hostname.c calls (SCNetworkReachabilityCreate
 * WithOptions/SetCallback/ScheduleWithRunLoop/UnscheduleFromRunLoop/
 * CopyResolvedAddress).
 *
 * Real SCNetworkReachability.c (SystemConfiguration.fproj) is a real,
 * substantial (2068-line) subsystem -- real BSD routing-socket
 * monitoring, host resolution, kernel network-reachability-flag
 * computation -- already deliberately excluded from
 * libSystemConfiguration.dylib as its own separate port
 * (build_libsystemconfiguration_client.sh's own header comment lists it
 * under "NOT in scope this pass (genuinely separate, much larger real
 * subsystems)"), not something DAR-241 should silently duplicate or
 * shortcut with a full reimplementation.
 *
 * Real-behavior justification (DAR-241, 2026-09-04, read set-hostname.c
 * in full before writing this): set-hostname.c's real update_hostname()
 * has an EXISTING, real 4-step fallback chain for computing the local
 * hostname, in order: (1) an explicit hostname set in preferences, (2) a
 * **real, DHCP-provided** hostname (copy_dhcp_hostname(), reading a
 * dynamic-store value IPConfiguration/bootp already populated from a
 * real DHCP lease's `host-name` option -- completely untouched by this
 * file, still 100% real and functional), (3) a reverse-DNS PTR lookup of
 * the primary IP via SCNetworkReachability, (4) the multicast-DNS local
 * hostname, (5) a hardcoded "localhost" fallback. ptr_query_start()
 * ALREADY has a real, existing error path for "reachability object
 * could not be created" (`if (ptrTarget == NULL) { ...; return FALSE; }`,
 * set-hostname.c ~line 428) which its caller ALREADY treats as
 * "PTR lookup didn't start, fall through to the mDNS/localhost steps"
 * (~line 486: `if (ok) goto done;` -- falls through when ok is FALSE).
 * Making SCNetworkReachabilityCreateWithOptions() return NULL here drives
 * exactly that already-real, already-correct degraded path -- it does
 * NOT touch step (2), the actual DHCP-provided-hostname path, at all.
 * ptr_query_stop() (the only caller of the other 4 functions) itself
 * guards with `if (ptrTarget == NULL) return;` (~line 213), so with
 * Create always returning NULL, SetCallback/ScheduleWithRunLoop/
 * UnscheduleFromRunLoop/CopyResolvedAddress are real, linkable, but
 * NEVER actually invoked at runtime -- narrow, honestly-documented
 * "real subsystem out of scope, real existing fallback chain still
 * intact" stand-in, not a DHCP/network-config-relevant shortcut.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SystemConfiguration.h>

SCNetworkReachabilityRef
SCNetworkReachabilityCreateWithOptions(CFAllocatorRef allocator, CFDictionaryRef options)
{
	(void)allocator;
	(void)options;
	return NULL;
}

Boolean
SCNetworkReachabilitySetCallback(SCNetworkReachabilityRef target,
				 SCNetworkReachabilityCallBack callback,
				 SCNetworkReachabilityContext *context)
{
	(void)target;
	(void)callback;
	(void)context;
	return FALSE;
}

Boolean
SCNetworkReachabilityScheduleWithRunLoop(SCNetworkReachabilityRef target,
					 CFRunLoopRef runLoop,
					 CFStringRef runLoopMode)
{
	(void)target;
	(void)runLoop;
	(void)runLoopMode;
	return FALSE;
}

Boolean
SCNetworkReachabilityUnscheduleFromRunLoop(SCNetworkReachabilityRef target,
					   CFRunLoopRef runLoop,
					   CFStringRef runLoopMode)
{
	(void)target;
	(void)runLoop;
	(void)runLoopMode;
	return FALSE;
}

CFArrayRef
SCNetworkReachabilityCopyResolvedAddress(SCNetworkReachabilityRef target, int *error_num)
{
	(void)target;
	if (error_num != NULL) {
		*error_num = 0;
	}
	return NULL;
}

/*
 * DAR-241 follow-up (same session): dnsinfo/dnsinfo_create.c's
 * _dns_resolver_set_reach_flags() also calls SCNetworkReachabilityGetFlags(),
 * but only ever AFTER a successful SCNetworkReachabilityCreateWithOptions()
 * -- since that always returns NULL here, `target` is always NULL and
 * _dns_resolver_set_reach_flags() already `break`s out (via its own real
 * "SCNetworkReachabilityCreateWithOptions() failed" error path) before
 * ever reaching this call. Real, linkable, but never actually invoked at
 * runtime -- same reasoning as the other 5 functions in this file.
 */
Boolean
SCNetworkReachabilityGetFlags(SCNetworkReachabilityRef target, SCNetworkReachabilityFlags *flags)
{
	(void)target;
	if (flags != NULL) {
		*flags = 0;
	}
	return FALSE;
}
