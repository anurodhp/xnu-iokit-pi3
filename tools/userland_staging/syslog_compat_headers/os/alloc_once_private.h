/* Real os_alloc_once_private.h forwarder -- the real alloc-once mechanism
 * (_os_alloc_once/os_alloc_once, struct _os_alloc_once_s, OS_ALLOC_ONCE_KEY_MAX)
 * is genuinely present in this checkout at
 * third_party/libplatform/private/os/alloc_once_impl.h; only the actual
 * Apple header this project's pinned notify_client.c #includes under a
 * different, SPI-versioned name is missing (confirmed absent from every
 * pinned repo, not just under another name -- grepped third_party/ for
 * OS_ALLOC_ONCE_KEY_LIBSYSTEM_NOTIFY, zero hits outside Libnotify's own
 * consumer site). Real per-consumer key IDs are just Apple's own private,
 * arbitrary small-int registry (indices into a fixed OS_ALLOC_ONCE_KEY_MAX
 * slot table) -- assigning this project's own unused slot for the one
 * real consumer syslogd/daemon.c actually needs (notify_client.c's own
 * os_alloc_once(OS_ALLOC_ONCE_KEY_LIBSYSTEM_NOTIFY, ...) call) is safe:
 * nothing else in this project's static link currently claims a slot.
 */
#ifndef _OS_ALLOC_ONCE_PRIVATE_COMPAT_H_
#define _OS_ALLOC_ONCE_PRIVATE_COMPAT_H_

#define __OS_ALLOC_INDIRECT__
#include <os/alloc_once_impl.h>
#undef __OS_ALLOC_INDIRECT__

#define OS_ALLOC_ONCE_KEY_LIBSYSTEM_NOTIFY 2

#endif
