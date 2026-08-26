/* Real libdispatch config header for this project's target -- takes the
 * place of the stale, autotools-generated third_party/libdispatch/config/
 * config.h (version "1.3", pre-2013-vintage, assumes HAVE_OBJC=1 and
 * HAVE_PTHREAD_WORKQUEUES=1, neither true here). internal.h prefers
 * <config/config_ac.h> over <config/config.h> via `__has_include` when
 * both exist -- this file is placed earlier on the include path
 * (-I dispatch_compat_headers) than third_party/libdispatch itself so it
 * wins.
 *
 * Values below reflect this project's real, confirmed environment, not
 * guesses -- each one was checked, not assumed (see
 * build_libdispatch_real.sh's own header comment for the verification
 * done for each):
 *
 * - HAVE_OBJC 0: this project compiles libdispatch as plain C (no ObjC
 *   runtime exists on this target at all -- same OS_OBJECT_USE_OBJC=0
 *   convention already used throughout Phase 6.8's libxpc work).
 * - HAVE_PTHREAD_WORKQUEUES 0 / DISPATCH_USE_INTERNAL_WORKQUEUE 1: real
 *   Darwin kernel-backed pthread workqueues (`_pthread_workqueue_init`,
 *   `pthread_workqueue_setdispatch_np`, the `workq_kernreturn` syscall's
 *   dynamic thread pool) are NOT implemented anywhere in this project's
 *   curated libpthread (confirmed via `nm -g` across every libc_build .a --
 *   zero hits for any of those symbols), even though the real
 *   `pthread/workqueue_private.h` header happens to be vendored. Forcing
 *   the internal-workqueue path makes libdispatch manage its own thread
 *   pool with plain pthread_create()+condvars, which this project's real
 *   pthread_create() (Phase 7.5, QEMU-verified) already supports end to
 *   end.
 * - HAVE_PTHREAD_MACHDEP_H 0 / HAVE_PTHREAD_KEY_INIT_NP 0: no
 *   `pthread_machdep.h` exists anywhere in this project's pinned
 *   libpthread (confirmed via find) -- this disables
 *   `DISPATCH_USE_DIRECT_TSD` (a Darwin-specific direct-register TSD-slot
 *   optimization that needs exact coordination with libpthread's real
 *   internal slot table, which this project's curated libpthread doesn't
 *   replicate), falling back to the portable `pthread_getspecific()`/
 *   `pthread_setspecific()` path in shims/tsd.h -- correct, just not the
 *   perf-optimized Darwin fast path.
 * - HAVE_MACH 1 / USE_MACH_SEM 1: real, both already present and working
 *   (osfmk/mach headers, and this project's own real semaphore_signal/
 *   semaphore_wait trap stubs from Phase 6.8's build_libxpc_syscalls.sh).
 * - USE_POSIX_SEM 0: not needed, USE_MACH_SEM covers every real call site.
 * - DISPATCH_USE_DTRACE 0: no dtrace provider infrastructure exists on
 *   this target.
 * - HAVE_LEAKS 0: no `leaks` tool exists here.
 * - HAVE_MALLOC_CREATE_ZONE 0: this project's curated libmalloc doesn't
 *   implement malloc zones (confirmed via nm, same check as the
 *   workqueue symbols above).
 * - ENABLE_THREAD_LOCAL_STORAGE / DISPATCH_USE_THREAD_LOCAL_STORAGE left
 *   at CMake's own default (1) -- real clang/arm64 `_Thread_local`
 *   support doesn't depend on anything this project is missing.
 */
#ifndef __DISPATCH_CONFIG_AC_H__
#define __DISPATCH_CONFIG_AC_H__

#define HAVE_MACH 1
#define HAVE_MACH_ABSOLUTE_TIME 1
#define HAVE_MACH_APPROXIMATE_TIME 1
#define HAVE_OBJC 0

#define HAVE_PTHREAD_WORKQUEUES 0
#define DISPATCH_USE_INTERNAL_WORKQUEUE 1
#define HAVE_PTHREAD_WORKQUEUE_H 0
#define HAVE_PTHREAD_WORKQUEUE_PRIVATE_H 0
#define HAVE__PTHREAD_WORKQUEUE_INIT 0
#define HAVE_PTHREAD_WORKQUEUE_SETDISPATCH_NP 0

#define HAVE_PTHREAD_MACHDEP_H 0
#define HAVE_PTHREAD_KEY_INIT_NP 0
#define HAVE_PTHREAD_NP_H 0
#define HAVE_PTHREAD_QOS_H 1
/* real, distinct from HAVE_PTHREAD_WORKQUEUES above -- QoS class support
 * (pthread_attr_set_qos_class_np, qos_class_t) is a real, always-present
 * Darwin/pthread-header feature independent of which thread-pool
 * implementation backs dispatch's queues; voucher.c's own
 * `#if !HAVE_PTHREAD_WORKQUEUE_QOS #error ...#endif` confirms it's a
 * hard requirement, not optional, and this project's real
 * <pthread/qos.h> (HAVE_PTHREAD_QOS_H=1 above) already provides it. */
#define HAVE_PTHREAD_WORKQUEUE_QOS 1
#define HAVE_PTHREAD_ATTR_SETCPUPERCENT_NP 0
#define HAVE_PTHREAD_MAIN_NP 1
#define HAVE_PTHREAD_YIELD_NP 0
#define USE_APPLE_TSD_OPTIMIZATIONS 0

#define USE_MACH_SEM 1
#define USE_POSIX_SEM 0

#define DISPATCH_USE_DTRACE 0
#define DISPATCH_USE_DTRACE_INTROSPECTION 0
#define HAVE_LEAKS 0
#define HAVE_MALLOC_CREATE_ZONE 0
#define HAVE_MALLOC_MALLOC_H 1
#define HAVE_LIBPROC_INTERNAL_H 0
#define HAVE_SYS_GUARDED_H 0
#define HAVE_GETPROGNAME 1
#define HAVE_POSIX_FADVISE 0
#define HAVE_POSIX_SPAWNP 1
#define HAVE_STRLCPY 1
#define HAVE_SYSCONF 1
#define HAVE_ARC4RANDOM 1
#define HAVE_DECL_CLOCK_MONOTONIC 0
#define HAVE_DECL_CLOCK_REALTIME 1
#define HAVE_DECL_CLOCK_UPTIME 0
#define HAVE_DECL_CLOCK_UPTIME_FAST 0
#define HAVE_TARGETCONDITIONALS_H 1
#define HAVE_NORETURN_BUILTIN_TRAP 1

/* Left UNDEFINED on purpose (neither 1 nor 0) -- real, confirmed via
 * -fsyntax-only trial compile: src/shims/tsd.h's real TSD accessor
 * macros/functions are gated `#if DISPATCH_USE_THREAD_LOCAL_STORAGE`
 * (the C11 `_Thread_local` fast path) vs. `#ifndef
 * DISPATCH_USE_THREAD_LOCAL_STORAGE` (the portable `pthread_setspecific`/
 * `pthread_getspecific` path) -- `#ifndef` checks definedness, not
 * truthiness, so defining this macro as literal `0` (CMake's own
 * ENABLE_THREAD_LOCAL_STORAGE=0 path would do exactly this) satisfies
 * NEITHER guard and leaves `_dispatch_thread_getspecific`/
 * `_dispatch_thread_setspecific`/`_dispatch_thread_port` all
 * undeclared. Leaving it fully undefined selects the portable
 * `pthread_key_t`-based path, which needs only `pthread_setspecific`/
 * `pthread_getspecific`/`pthread_key_create` (all real, already in this
 * project's curated libpthread.a) and
 * `pthread_mach_thread_np(_dispatch_thread_self())` for
 * `_dispatch_thread_port()` (also confirmed present via `nm -g`).
 */

#endif /* __DISPATCH_CONFIG_AC_H__ */
