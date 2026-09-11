/*
 * DAR-283: narrow stand-in for the 9 SCNetworkReachability entry points
 * scutil's `-r` / `-W -r` (check / watch reachability) commands call.
 *
 * WHY NOT THE REAL FILE
 * =====================
 * SystemConfiguration.fproj/SCNetworkReachability.c is real, pinned,
 * unmodified Apple source, and it is deliberately NOT part of this
 * project's libSystemConfiguration.dylib -- see
 * build_libsystemconfiguration_client.sh's own scope statement, which
 * lists it under "genuinely separate, much larger real subsystems"
 * (2068 lines: BSD routing-socket monitoring, host resolution, kernel
 * reachability-flag computation). It also cannot simply be compiled in
 * here: its own private header, SCNetworkReachabilityInternal.h, builds
 * every reachability object out of libnetwork's `nw_endpoint_t` /
 * `nw_parameters_t` / `nw_path_evaluator_t` / `nw_resolver_t` types from
 * <nw/private.h>, which Apple has never open-sourced (this project
 * already carries a types-only stand-in for that header at
 * configd_plugin_compat/nw/private.h, written for DAR-241 -- enough to
 * type-check a struct declaration, nowhere near enough to implement the
 * subsystem).
 *
 * WHY RETURNING NULL IS REAL APPLE BEHAVIOUR HERE
 * ===============================================
 * Every scutil reachability command funnels through tests.c's
 * _setupReachability(), and both callers already have a real,
 * Apple-authored error path for a NULL target:
 *
 *     target = _setupReachability(argc, argv, NULL);
 *     if (target == NULL) {
 *             SCPrint(TRUE, stderr, CFSTR("  Could not determine status: %s\n"),
 *                     SCErrorString(SCError()));
 *             exit(1);
 *     }
 *
 * -- the same message a real macOS prints when reachability object
 * creation genuinely fails. `scutil -r <host>` therefore reports a real
 * failure and exits non-zero rather than silently printing a fabricated
 * "Reachable" verdict, which is the only outcome that would actually be
 * dishonest. _SCErrorSet(kSCStatusFailed) makes SCErrorString() print a
 * real, accurate SC status rather than a stale one.
 *
 * GetFlags/GetInterfaceIndex/SetCallback/ScheduleWithRunLoop/
 * SetDispatchQueue are only ever reached through a non-NULL target, so
 * they are real, linkable, and never invoked at runtime -- the same
 * shape as configd_plugin_reachability_compat.c's own documented
 * stand-ins (a separate file on purpose: that one is linked into
 * configd.macho, this one into scutil.macho, and neither image links
 * the other's copy).
 */
#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SystemConfiguration.h>
#include <SystemConfiguration/SCPrivate.h>

SCNetworkReachabilityRef
SCNetworkReachabilityCreateWithAddress(CFAllocatorRef allocator, const struct sockaddr *address)
{
	(void)allocator; (void)address;
	_SCErrorSet(kSCStatusFailed);
	return NULL;
}

SCNetworkReachabilityRef
SCNetworkReachabilityCreateWithAddressPair(CFAllocatorRef allocator,
					   const struct sockaddr *localAddress,
					   const struct sockaddr *remoteAddress)
{
	(void)allocator; (void)localAddress; (void)remoteAddress;
	_SCErrorSet(kSCStatusFailed);
	return NULL;
}

SCNetworkReachabilityRef
SCNetworkReachabilityCreateWithName(CFAllocatorRef allocator, const char *nodename)
{
	(void)allocator; (void)nodename;
	_SCErrorSet(kSCStatusFailed);
	return NULL;
}

SCNetworkReachabilityRef
SCNetworkReachabilityCreateWithOptions(CFAllocatorRef allocator, CFDictionaryRef options)
{
	(void)allocator; (void)options;
	_SCErrorSet(kSCStatusFailed);
	return NULL;
}

Boolean
SCNetworkReachabilityGetFlags(SCNetworkReachabilityRef target, SCNetworkReachabilityFlags *flags)
{
	(void)target;
	if (flags != NULL) {
		*flags = 0;
	}
	_SCErrorSet(kSCStatusFailed);
	return FALSE;
}

int
SCNetworkReachabilityGetInterfaceIndex(SCNetworkReachabilityRef target)
{
	(void)target;
	_SCErrorSet(kSCStatusFailed);
	return -1;
}

Boolean
SCNetworkReachabilitySetCallback(SCNetworkReachabilityRef target,
				 SCNetworkReachabilityCallBack callback,
				 SCNetworkReachabilityContext *context)
{
	(void)target; (void)callback; (void)context;
	_SCErrorSet(kSCStatusFailed);
	return FALSE;
}

Boolean
SCNetworkReachabilityScheduleWithRunLoop(SCNetworkReachabilityRef target,
					 CFRunLoopRef runLoop,
					 CFStringRef runLoopMode)
{
	(void)target; (void)runLoop; (void)runLoopMode;
	_SCErrorSet(kSCStatusFailed);
	return FALSE;
}

Boolean
SCNetworkReachabilitySetDispatchQueue(SCNetworkReachabilityRef target, dispatch_queue_t queue)
{
	(void)target; (void)queue;
	_SCErrorSet(kSCStatusFailed);
	return FALSE;
}
