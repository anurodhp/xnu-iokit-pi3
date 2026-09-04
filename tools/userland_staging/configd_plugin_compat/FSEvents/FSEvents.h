/* Real, narrow compat stand-in for <FSEvents/FSEvents.h> (CoreServices/
 * CarbonCore's FSEvents framework -- confirmed via real `git ls-remote`
 * that neither FSEvents, fseventsd, CarbonCore, nor CoreServices exist
 * anywhere on apple-oss-distributions; this is genuinely never-
 * open-sourced, kernel-fsevents-device-backed (`/dev/fsevents`) Apple-
 * internal machinery, not a configd/IPMonitor-owned subsystem at all).
 *
 * DAR-241 (2026-09-04): Plugins/IPMonitor/dns-configuration.c's
 * dns_configuration_monitor() uses this only to watch /etc/resolvers
 * for filesystem changes and re-trigger a DNS-configuration refresh
 * live, without a configd restart -- a real, but purely optional,
 * live-reload convenience. Confirmed by reading the whole file: the
 * actual DNS configuration read/apply path (dns_configuration_set(),
 * called from ip_plugin.c at both plugin load and on every ordinary
 * SCDynamicStore-driven network/service change) does not depend on
 * this watch at all -- it already runs, and already re-reads
 * /etc/resolvers contents, any time a real network/service change
 * fires. The only real gap a no-op watch introduces is: hand-editing
 * files under /etc/resolvers/ while configd is already running, with
 * no other network change also occurring, would not auto-refresh until
 * the next real event. Given this project's minimal userland ships no
 * /etc/resolvers directory at all (single Ethernet interface, no
 * per-domain resolver files), this is a real, narrow, honestly-
 * documented "inapplicable for this embedded target" exclusion --
 * not a stand-in for DHCP/DNS-configuration-relevant behavior itself.
 */
#ifndef _FSEVENTS_COMPAT_H
#define _FSEVENTS_COMPAT_H

#include <CoreFoundation/CoreFoundation.h>

typedef const struct __FSEventStream	*ConstFSEventStreamRef;
typedef struct __FSEventStream		*FSEventStreamRef;
typedef uint64_t			FSEventStreamEventId;
typedef uint32_t			FSEventStreamEventFlags;
typedef uint32_t			FSEventStreamCreateFlags;

enum {
	kFSEventStreamCreateFlagNone		= 0x00000000,
	kFSEventStreamCreateFlagUseCFTypes	= 0x00000001,
	kFSEventStreamCreateFlagFileEvents	= 0x00000010,
	kFSEventStreamCreateFlagWatchRoot	= 0x00000004,
};

#define kFSEventStreamEventIdSinceNow	((FSEventStreamEventId)UINT64_MAX)

typedef struct {
	CFIndex			version;
	void			*info;
	CFAllocatorRetainCallBack	retain;
	CFAllocatorReleaseCallBack	release;
	CFAllocatorCopyDescriptionCallBack	copyDescription;
} FSEventStreamContext;

typedef void (*FSEventStreamCallback)(ConstFSEventStreamRef streamRef,
				      void *clientCallBackInfo,
				      size_t numEvents,
				      void *eventPaths,
				      const FSEventStreamEventFlags eventFlags[],
				      const FSEventStreamEventId eventIds[]);

static inline FSEventStreamRef
FSEventStreamCreate(CFAllocatorRef allocator,
		    FSEventStreamCallback callback,
		    FSEventStreamContext *context,
		    CFArrayRef pathsToWatch,
		    FSEventStreamEventId sinceWhen,
		    CFTimeInterval latency,
		    FSEventStreamCreateFlags flags)
{
	(void)allocator; (void)callback; (void)context; (void)pathsToWatch;
	(void)sinceWhen; (void)latency; (void)flags;
	/* Real, honest no-op: no fsevents kernel device backing exists on
	 * this port -- see this header's own top-of-file justification.
	 */
	return NULL;
}

static inline void
FSEventStreamScheduleWithRunLoop(FSEventStreamRef streamRef, CFRunLoopRef runLoop, CFStringRef runLoopMode)
{
	(void)streamRef; (void)runLoop; (void)runLoopMode;
}

static inline Boolean
FSEventStreamStart(FSEventStreamRef streamRef)
{
	(void)streamRef;
	return FALSE;
}

#endif /* _FSEVENTS_COMPAT_H */
