/* Real, always-safe implementation of _dyld_is_memory_immutable() for
 * this project's fully-static (no dyld) build -- see
 * syslog_compat_headers/mach-o/dyld_priv.h for the real call-site
 * justification (notify_client.c's _notify_strdup_if_mutable()). */
#include <stdbool.h>
#include <stddef.h>

bool _dyld_is_memory_immutable(const void *addr, size_t length) {
    (void)addr;
    (void)length;
    return false;
}
