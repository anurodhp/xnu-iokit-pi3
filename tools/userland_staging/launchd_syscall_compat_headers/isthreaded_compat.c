/* Real Apple libc global (declared extern in various FreeBSD-derived
 * Libc files, e.g. gen/FreeBSD/ttyname.c, stdlib/FreeBSD/atexit.c) used
 * as a performance optimization: skip locking when the process is known
 * to still be single-threaded. This project's curated libc stack never
 * defined it (no consumer needed it before atexit.c/glob.c). Real, safe
 * default: always report "threaded" -- this project's pthread support
 * is real (pthread_create works), so always taking the lock path is
 * always correct, just not maximally optimized for the single-thread
 * case. Never wrong, unlike guessing 0.
 */
int __isthreaded = 1;

/* ------------------------------------------------------------------
 * REAL BUG FOUND + FIXED 2026-08-30 (QEMU boot test, see
 * docs/launchd-pid1-postmortem.md): atexit() hung forever, in EVERY
 * binary in this project that links liblaunchd_syscalls.a's atexit.o.
 *
 * Real Apple atexit.c's atexit_register() opens with
 *     struct atexit *p = __atexit;
 *     assert(p);
 * and `__atexit` is a file-static that is ONLY ever set by
 * __atexit_init() (same file), which real Apple calls from exactly one
 * place: _libc_initializer() in Libc/sys/_libc_init.c. This project has
 * never built _libc_init.c -- it needs seven more real initializers
 * (_program_vars_init/_libc_fork_init/__confstr_init/_init_clock_port/
 * __chk_init/__xlocale_init/__guard_setup/_subsystem_init) whose
 * supporting Libc files aren't in this port's libc tier -- so __atexit
 * stayed NULL and the assert fired on the very first atexit() call. On
 * this port assert()'s abort() is kill(getpid(), SIGABRT) with no real
 * signal delivery yet, so instead of dying it spins forever: a totally
 * silent hang, with launchd's stderr on /dev/null.
 *
 * Why it went unnoticed until now: launchd's only atexit() call site on
 * the boot path is ipc_server_init()'s atexit(ipc_clean_up), and that
 * function bailed out several statements earlier at
 * `mkdir(LAUNCHD_SOCK_PREFIX)` returning EROFS, because the root
 * filesystem was still read-only at that point. Fixing that ordering
 * (pd_launchd_boot()'s `mount -uw /` equivalent now runs before
 * ipc_server_init(), see launchd.c) made the atexit() call reachable for
 * the first time and turned the boot into a silent hang.
 *
 * The fix is the one real initializer step atexit.c actually needs, run
 * as a real image constructor. Deliberately NOT a reimplementation of
 * _libc_initializer(): the other seven steps have their own real
 * prerequisites and their own real consumers, and inventing partial
 * stand-ins for them would be exactly the kind of fabrication this
 * project's conventions forbid. This calls the genuine
 * __atexit_init() -- real, unmodified Apple source, already compiled
 * into the same image from Libc/stdlib/FreeBSD/atexit.c -- and nothing
 * else. It is idempotent by construction (__atexit_init() is a single
 * os_alloc_once(OS_ALLOC_ONCE_KEY_LIBSYSTEM_C, ...) call, which returns
 * the same block on every call), so a second caller appearing later is
 * harmless.
 *
 * Why THIS file: __atexit_init is __private_extern__ in atexit.c, so
 * only another object in the SAME link can reach it -- it is not
 * callable from a dylib. And `__isthreaded` above is referenced by
 * atexit.c itself (its _MUTEX_LOCK/_MUTEX_UNLOCK macros), so this object
 * is pulled out of liblaunchd_syscalls.a by the linker exactly when
 * atexit.o is and never when it isn't. No separate force_load, and no
 * risk of the constructor being linked into a binary that has no atexit
 * at all.
 */
extern void __atexit_init(void);

__attribute__((constructor))
static void
launchd_syscalls_atexit_init(void)
{
	__atexit_init();
}
