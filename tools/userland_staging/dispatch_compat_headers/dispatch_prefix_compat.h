/* Forced prefix header for the real libdispatch port (-include'd on
 * every translation unit by build_libdispatch_real.sh) -- closes a
 * handful of real, narrow gaps found via real -fsyntax-only trial
 * compiles of src/{once,semaphore,apply,time,allocator,...}.c against
 * this project's SDK+xnu-7195 headers, each confirmed real (not
 * guessed) before being added here:
 *
 * - <os/tsd.h>: src/shims/tsd.h's own `_dispatch_cpu_number()` checks
 *   `__has_include(<os/tsd.h>)` UNCONDITIONALLY (independent of this
 *   project's USE_APPLE_TSD_OPTIMIZATIONS=0/HAVE_PTHREAD_KEY_INIT_NP=0
 *   config, which only gates a DIFFERENT, earlier `#include <os/tsd.h>`
 *   in the same file) -- since the real file genuinely exists on this
 *   project's include path at third_party/xnu-7195/libsyscall/os/tsd.h
 *   (real, complete, with a real arm64 `_os_cpu_number()` reading
 *   TPIDRRO_EL0 -- the same register this project's own real pthread
 *   TSD bootstrap, Phase 7.5, already uses), `__has_include` reports
 *   true but the header was never actually `#include`d, leaving
 *   `_os_cpu_number()` undeclared. Including it here unconditionally
 *   matches what `__has_include` already promises callers.
 * - `kdebug_trace()`: real declaration + tiny inline forwarding to the
 *   already-built raw trap stubs (`___kdebug_trace64`/
 *   `___kdebug_trace_string`, present in both libc_build/dyld_syscall_gen
 *   and libc_build/pthread_syscall_gen from earlier ports) -- confirmed
 *   real signature from third_party/xnu-7195/libsyscall/wrappers/
 *   kdebug_trace.c.
 * - `mach_get_times()`: really, unconditionally declared in
 *   third_party/xnu-7195/osfmk/mach/mach_time.h, but this project's
 *   pinned iPhoneOS14.4 SDK ships its OWN older `mach/mach_time.h`
 *   (found first via the normal sysroot search, `-idirafter` can't win
 *   against a same-named file the SDK already has) that predates this
 *   function -- forward-declared here instead of fighting header search
 *   order, since only the declaration (not the real xnu source body) is
 *   needed at compile time; real mach_get_times() is a Mach trap
 *   already implemented in the kernel this project boots.
 * - `_COMM_PAGE_CPU_QUIESCENT_COUNTER`: real address constant from
 *   third_party/xnu-7195/osfmk/arm/cpu_capabilities.h
 *   (`_COMM_PAGE_START_ADDRESS + 0x180`, with the real arm64
 *   `_COMM_PAGE64_BASE_ADDRESS` value) -- that header itself is
 *   kernel/commpage-implementation-facing (defines the physical commpage
 *   layout for xnu itself to populate), not meant for userland
 *   inclusion, so the one needed constant is copied here rather than
 *   including the whole kernel header.
 * - firehose/private.h's real-enough type stand-ins (see that file's own
 *   header comment) are force-included here too, since
 *   voucher_internal.h needs them UNCONDITIONALLY (not just under
 *   OS_VOUCHER_ACTIVITY_SPI/OS_FIREHOSE_SPI, which this project disables
 *   -- see build_libdispatch_real.sh's own header comment for why).
 */
#ifndef __DISPATCH_PREFIX_COMPAT_H__
#define __DISPATCH_PREFIX_COMPAT_H__

#include <os/tsd.h>
#include "firehose/private.h"

/* real os_unlikely()/os_likely()/os_fastpath()/os_slowpath() macros --
 * needed unconditionally by event/event_kevent.c (confirmed via real
 * -fsyntax-only trial: "implicit declaration of function 'os_unlikely'",
 * meaning the macro was never expanded at all at that call site, unlike
 * every other core dispatch file which happens to reach this same real
 * header transitively via os/assumes.h -idirafter's on this project's
 * own $LIBC path). Forced here so every translation unit gets it
 * regardless of its own particular include chain. */
#include "os/base_private.h"

#include <stdint.h>
#include <mach/kern_return.h>
#include <mach/mach_types.h>
#include <time.h>

/* real Apple's firehose/private.h also defines this version constant
 * (init.c's own real `_firehose_spi_version` global, dead weight on
 * this target since no logd exists, still needs SOME real value to
 * compile) -- using the same real date-stamped-version convention
 * os/voucher_activity_private.h already establishes for
 * OS_VOUCHER_ACTIVITY_SPI_VERSION (20161003). */
#ifndef OS_FIREHOSE_SPI_VERSION
#define OS_FIREHOSE_SPI_VERSION 20161003
#endif

/* real declaration, osfmk/mach/mach_traps.h:90 -- this project's pinned
 * iPhoneOS14.4 SDK ships its own older <mach/mach_traps.h> (found first
 * via the normal sysroot search, same class of shadow as
 * mach_get_times() above) that predates this trap. */
extern mach_port_name_t thread_get_special_reply_port(void);

/* real declarations, third_party/libpthread/private/pthread/
 * workqueue_private.h -- not `#include`d by libdispatch's own
 * internal.h at all (confirmed via grep), yet src/queue.c calls
 * `_pthread_workloop_create`/`_pthread_workloop_destroy`
 * UNCONDITIONALLY under `#if TARGET_OS_MAC` (the workloop
 * kernel-scheduler-attributes path, not gated by this project's
 * DISPATCH_USE_INTERNAL_WORKQUEUE choice at all). */
#include <pthread/workqueue_private.h>

extern int __kdebug_trace64(uint32_t code, uint64_t arg1, uint64_t arg2,
    uint64_t arg3, uint64_t arg4);

static inline int
kdebug_trace(uint32_t debugid, uint64_t arg1, uint64_t arg2, uint64_t arg3,
    uint64_t arg4)
{
	return __kdebug_trace64(debugid, arg1, arg2, arg3, arg4);
}

extern kern_return_t mach_get_times(uint64_t *absolute_time,
    uint64_t *cont_time, struct timespec *tp);

#define _COMM_PAGE64_BASE_ADDRESS_COMPAT (0x0000000FFFFFC000ULL)
#define _COMM_PAGE_CPU_QUIESCENT_COUNTER (_COMM_PAGE64_BASE_ADDRESS_COMPAT + 0x180)

/* real, unmodified value from os/voucher_activity_private.h -- that
 * whole header is gated `#if OS_VOUCHER_ACTIVITY_SPI` (disabled on this
 * target), but src/voucher.c and src/queue.c both reference this exact
 * sentinel pointer UNCONDITIONALLY (confirmed via grep) -- same class of
 * "needed regardless of the tracing SPI flag" gap as
 * firehose_activity_id_t above. Macro body only needs `voucher_t`/
 * `OS_OBJECT_BRIDGE` to resolve at each real USE site (later in the
 * translation unit, well after they're both defined), not here at this
 * prefix header's own inclusion point.
 */
#define VOUCHER_CURRENT ((OS_OBJECT_BRIDGE voucher_t)(void *)~2ul)

#endif /* __DISPATCH_PREFIX_COMPAT_H__ */
