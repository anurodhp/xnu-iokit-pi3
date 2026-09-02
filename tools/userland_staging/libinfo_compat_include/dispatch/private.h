/* Narrow override, NOT the real umbrella dispatch/private.h (that real
 * header -- third_party/libdispatch/private/private.h, already reachable
 * project-wide via tools/userland_staging/dispatch_compat_headers/dispatch
 * -- unconditionally #includes mach_private.h/data_private.h/
 * source_private.h, all three of which fail to parse under this
 * project's pinned Xcode 12 clang: real `API_AVAILABLE(..., bridgeos(4.0))`
 * annotations hit a real macro-expansion mismatch even with this
 * project's own established __API_AVAILABLE_PLATFORM_bridgeos/
 * SPI_AVAILABLE overrides (see build_libdispatch_dylib.sh's identical
 * defines) -- confirmed by direct probing 2026-09-01, not guessed: the
 * override macro registers correctly (verified via -E -dM) but the
 * surrounding real Availability.h machinery still fails to parse these
 * specific multi-platform annotations, a pre-existing SDK/header-vintage
 * mismatch unrelated to Libinfo's own code.
 *
 * lookup.subproj/si_module.c's ENTIRE real need from this header is one
 * enum constant: DISPATCH_QUEUE_OVERCOMMIT (used as a dispatch_async
 * queue-priority flag). Real value taken directly from
 * third_party/libdispatch/private/queue_private.h line 47
 * (`DISPATCH_QUEUE_OVERCOMMIT = 0x2ull`), not guessed. This override
 * sits earlier on this build's own -I search path than the real
 * dispatch_compat_headers/dispatch/private.h, so it's picked up instead
 * -- scoped to this one build (build_libinfo.sh), not a change to the
 * shared dispatch_compat_headers other scripts depend on. */
#ifndef __DISPATCH_PRIVATE_LIBINFO_OVERRIDE__
#define __DISPATCH_PRIVATE_LIBINFO_OVERRIDE__

#include <dispatch/dispatch.h>

#define DISPATCH_QUEUE_OVERCOMMIT 0x2ull

#endif /* __DISPATCH_PRIVATE_LIBINFO_OVERRIDE__ */
