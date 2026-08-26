#ifndef __OS_BASE_PRIVATE__
#define __OS_BASE_PRIVATE__
/* Real, unmodified copy of third_party/xnu-7195/libkern/os/base_private.h
 * -- needed by third_party/Libc/os/assumes.h (real header, see
 * os_assumes_compat.c's own comment for why libxpc uses the real one
 * now), reachable via `-idirafter $XNU/libkern` for MOST consumers, but
 * libdispatch's own trial CFLAGS can't add that same -idirafter path
 * globally without also exposing xnu's kernel-flavored (and
 * userland-incompatible) os/atomic_private.h -- see
 * build_libdispatch_real.sh's own header comment for that specific
 * conflict. Small enough (4 real macros) to duplicate narrowly instead
 * of fighting search-path precedence.
 */
#ifndef KERNEL
#include <TargetConditionals.h>
#endif
#include <os/base.h>

#ifndef os_fastpath
#define os_fastpath(x) ((__typeof__(x))OS_EXPECT((long)(x), ~0l))
#endif
#ifndef os_slowpath
#define os_slowpath(x) ((__typeof__(x))OS_EXPECT((long)(x), 0l))
#endif
#ifndef os_likely
#define os_likely(x) OS_EXPECT(!!(x), 1)
#endif
#ifndef os_unlikely
#define os_unlikely(x) OS_EXPECT(!!(x), 0)
#endif

#endif // __OS_BASE_PRIVATE__
