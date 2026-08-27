/* Real gap: third_party/Libnotify/notify_client.c:57 #includes this for
 * _dyld_is_memory_immutable() alone (used once, _notify_strdup_if_mutable(),
 * to skip a defensive strdup for string literals known to live in an
 * immutable/read-only dyld segment). This project's syslogd/notify build
 * is fully static (no dyld involved at all), so the real answer is
 * always "not verified immutable" -- confirmed safe by reading the one
 * real call site directly: returning false only costs an extra,
 * functionally-identical malloc+memcpy, never a correctness difference.
 * Implementation in notify_dyld_compat.c. */
#ifndef __SYSLOGD_COMPAT_DYLD_PRIV_H__
#define __SYSLOGD_COMPAT_DYLD_PRIV_H__
#include <stdbool.h>
#include <stddef.h>
extern bool _dyld_is_memory_immutable(const void *addr, size_t length);
#endif
