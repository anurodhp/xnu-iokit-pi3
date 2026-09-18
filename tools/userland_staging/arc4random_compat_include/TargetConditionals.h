/* Scoped override for real Apple <TargetConditionals.h>, used ONLY when
 * compiling third_party/Libc/gen/FreeBSD/arc4random.c (DAR-247).
 *
 * ---- THIS FILE IS THE SINGLE SOURCE OF TRUTH. DO NOT COPY IT. ----
 * (same convention as os_log_compat_headers/os/log_private.h -- one real
 * file, -I'd in ahead of everything else, not duplicated per script.)
 *
 * The real problem this works around: arc4random.c:47-48 does
 *
 *     #if !TARGET_OS_DRIVERKIT
 *     #define OS_CRASH_ENABLE_EXPERIMENTAL_LIBTRACE 1
 *     #endif
 *
 * unconditionally (this project's SDK's real TargetConditionals.h always
 * gives TARGET_OS_DRIVERKIT=0 for an iOS-target compile, so this branch
 * always fires). That macro makes third_party/Libc/os/assumes.h #include
 * <os/log_private.h>, which chains into
 * third_party/xnu-7195/libkern/firehose/tracepoint_private.h, which
 * reads a real hardware comm-page constant
 * (_COMM_PAGE_ATM_DIAGNOSTIC_CONFIG) that has no meaning for this
 * freestanding userland libc build -- a real, unresolvable compile
 * error, not a missing include path.
 *
 * Confirmed (2026-09-17) this is a compile-time-only concern, not a
 * behavior change: arc4random.c's actual function bodies are compiled
 * with -DVARIANT_STATIC (both build_libsystem_c.sh and
 * build_libresolv.sh define it), which selects arc4random.c's non-Apple
 * fallback branch instead of its corecrypto/ccrng path (guarded
 * "defined(__APPLE__) && !defined(VARIANT_STATIC)", so it is not
 * compiled in at all here) -- and inside that fallback branch, every
 * os_crash() call site is itself further guarded "!defined(VARIANT_STATIC)"
 * with a plain abort() used in the VARIANT_STATIC case instead. So with
 * VARIANT_STATIC defined, os_crash() is never actually called anywhere
 * in this translation unit; only the header parse needed it. Forcing
 * TARGET_OS_DRIVERKIT=1 here just skips assumes.h's unreachable
 * os_log_pack/firehose branch at parse time -- it does not change which
 * code path actually runs.
 *
 * #include_next lets the real SDK TargetConditionals.h still define
 * every other macro normally; only TARGET_OS_DRIVERKIT is touched, and
 * only for this one file.
 */
#ifndef _ARC4RANDOM_TARGETCONDITIONALS_OVERRIDE_H_
#define _ARC4RANDOM_TARGETCONDITIONALS_OVERRIDE_H_
#include_next <TargetConditionals.h>
#undef TARGET_OS_DRIVERKIT
#define TARGET_OS_DRIVERKIT 1
#endif
