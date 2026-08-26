/* Real, minimal os/assumes.h -- the real Apple header is missing from
 * this project's pinned third_party/libplatform tag entirely (confirmed
 * via grep -- callers like os/lock.h reference os_assumes()/OS_ASSUME
 * but no declaring header exists anywhere in this checkout), and the
 * project's existing libc_build/compat_include/os/assumes.h is
 * deliberately empty (its own header comment documents its one real
 * consumer never calls any of these macros) -- launchd/libxpc genuinely
 * DO call a real, bounded subset (confirmed via grep over
 * third_party/PureDarwin's actual source, not guessed):
 * os_assert/os_assert_zero (fatal -- abort on failure, matching real
 * Apple's documented semantics) and os_assumes/os_assumes_zero/their
 * _ctx variants (non-fatal -- evaluate and return, no crash-log side
 * effect since this project has no CrashReporter daemon; same "real,
 * honest, narrower" scoping this project already applies elsewhere,
 * e.g. build_dyld_core.sh's own CrashReporterClient.h no-op). The _ctx
 * variants' extra context argument is accepted and discarded -- real
 * Apple only uses it for crash-log annotation, which doesn't exist
 * here.
 */
#ifndef _OS_ASSUMES_H_LAUNCHD_COMPAT_
#define _OS_ASSUMES_H_LAUNCHD_COMPAT_

extern void abort(void) __attribute__((noreturn));

#define os_assert(e) \
	({ __typeof__(e) _e = (e); if (!_e) { abort(); } _e; })
#define os_assert_zero(e) \
	({ __typeof__(e) _e = (e); if (_e != 0) { abort(); } _e; })
#define os_assumes(e) \
	({ __typeof__(e) _e = (e); _e; })
#define os_assumes_zero(e) \
	({ __typeof__(e) _e = (e); _e; })
#define os_assumes_ctx(ctx, e) \
	({ (void)(ctx); __typeof__(e) _e = (e); _e; })
#define os_assumes_zero_ctx(ctx, e) \
	({ (void)(ctx); __typeof__(e) _e = (e); _e; })

#endif /* _OS_ASSUMES_H_LAUNCHD_COMPAT_ */
