/* Real, canonical fork() (DAR-292, 2026-09-17) -- the real, single-owner
 * home for fork()/waitpid()/execlp(), which until this file existed no
 * dylib in this project exported: 84 of 101 shipped .macho binaries each
 * carry their own private static copy (measured 2026-09-17 via `nm`
 * across every built tools/userland_staging/*.macho, not guessed --
 * DAR-292's own ticket text cites 79 from 2026-09-11, already stale by
 * 5; see this project's dup_symbol_audit.sh for the up-to-date count).
 *
 * Real Apple source this is based on: third_party/Libc/sys/fork.c. Same
 * body as this project's own already-proven fork_compat.c
 * (tools/userland_staging/build_launchd_syscalls.sh, written for DAR-149/
 * DAR-200-era launchd child-hang/EXC_CORPSE fixes -- read THAT file's own
 * header for the full real Libsystem_atfork_prepare/parent/child order
 * investigation, not re-derived here), with exactly one real, deliberate
 * difference: this canonical version does NOT call dispatch_atfork_*.
 *
 * WHY dispatch_atfork_* IS NOT HERE -- A REAL, VERIFIED BUILD-ORDER
 * CYCLE, NOT AN OVERSIGHT
 * ====================================================================
 * DAR-292's own ticket text flagged this as the one real risk needing
 * verification before picking a canonical home, and it checked out real:
 * build_libdispatch_dylib.sh's own final link
 * (`-Wl,-reexport_library,"$OUT/system/libsystem_c.dylib"`, confirmed via
 * a real grep of that script, not assumed) makes libdispatch.dylib
 * REEXPORT this dylib -- i.e. libdispatch.dylib is built strictly AFTER,
 * and depends on, libsystem_c.dylib already existing. If THIS file
 * called dispatch_atfork_prepare/parent/child by direct symbol reference
 * the way build_launchd_syscalls.sh's private fork_compat.c does, this
 * dylib's own build would need to link libdispatch.dylib -- a real ld64
 * cycle (libsystem_c needs libdispatch, libdispatch needs libsystem_c),
 * confirmed structurally, not by a failed trial link this time, because
 * the reexport direction alone already proves it.
 *
 * A `-Wl,-U` flat-namespace deferral (this dylib's own established
 * mechanism for other cross-dylib calls, e.g. _asin/_sqrt to
 * libsystem_m.dylib above) was considered and rejected: unlike
 * libsystem_m.dylib, libdispatch.dylib is NOT unconditionally co-loaded
 * with every process that loads libsystem_c.dylib -- most of the 84
 * fork()-calling binaries this ticket is about do NOT link libdispatch.
 * An unresolved flat-namespace bind aborts dyld-940 at PROCESS LOAD time
 * unconditionally (this dylib's own ___part_load_locale/___ldpart_free_
 * extra history above is the proof, and build_scutil.sh's real gate on
 * _fork's flat-namespace deferral is the same lesson from the consumer
 * side) -- so deferring dispatch_atfork_* here would abort every plain
 * binary that merely calls fork() without ever touching libdispatch.
 *
 * Real, honest scope of what THIS canonical fork() gives every consumer
 * that switches to it, all three already-linked siblings of this exact
 * dylib (see build_libsystem_c.sh's own final link line -- zero new link
 * edges needed for any of these):
 *   _mach_fork_child            -- libsystem_kernel.dylib
 *   _pthread_atfork_{prepare,parent,child}{,_handlers}
 *                                -- libsystem_pthread.dylib
 *   _malloc_fork_{prepare,parent,child}
 *                                -- libsystem_malloc.dylib
 * That is a strict improvement over what any of the 84 static-copy
 * consumers has today (most never called ANY atfork handler at all --
 * confirmed by grep, the only prior real orchestration anywhere in this
 * project is launchd's own private, richer copy). It is a real, narrower
 * order than launchd's own copy, which also calls dispatch_atfork_* --
 * launchd stays on its private copy deliberately (PID 1, dispatch-heavy,
 * highest real risk in this codebase, explicitly out of this ticket's
 * pilot scope -- see DAR-292's Linear comment). Any future consumer that
 * needs the fuller dispatch-aware order should follow launchd's own
 * pattern, not this file's.
 *
 * __fork ITSELF: real xnu-7195 libsyscall/custom/__fork.s, compiled
 * directly into the SIBLING libsystem_malloc.dylib (build_libsystem_
 * malloc.sh, same DAR-292 change), specifically because that raw asm
 * updates `__current_pid` through a direct, LOCAL adrp/lo12 pair and
 * this project's real, already-18-consumer-proven `__current_pid` lives
 * there (sys_getpid.o) -- see that script's own header for the full real
 * trail on why libsystem_kernel.dylib (real Apple's own home for this
 * pair) was rejected for this pass specifically. libsystem_c.dylib
 * already links libsystem_malloc.dylib directly, so `extern int
 * __fork(void)` below is a normal, already-established cross-dylib call,
 * not a new dependency. */
extern int __fork(void);

extern void _pthread_atfork_prepare_handlers(void);
extern void _pthread_atfork_prepare(void);
extern void _pthread_atfork_parent(void);
extern void _pthread_atfork_parent_handlers(void);
extern void _pthread_atfork_child(void);
extern void _pthread_atfork_child_handlers(void);
extern int  _mach_fork_child(void);
extern void _malloc_fork_prepare(void);
extern void _malloc_fork_parent(void);
extern void _malloc_fork_child(void);

/* Real Libsystem init.c order (same verbatim order this project's own
 * launchd fork_compat.c already established from real Libsystem-
 * 1292.50.1's init.c -- see that file's header for the full list),
 * minus dispatch_atfork_(), dyld_atfork_(), cc_atfork_(), and everything
 * else this project genuinely does not have (same real absences that file's own
 * header already documents symbol-by-symbol; not re-justified here). */
static void libSystem_atfork_prepare(void)
{
	_pthread_atfork_prepare_handlers();
	_malloc_fork_prepare();
	_pthread_atfork_prepare();
}

static void libSystem_atfork_parent(void)
{
	_pthread_atfork_parent();
	_malloc_fork_parent();
	_pthread_atfork_parent_handlers();
}

static void libSystem_atfork_child(void)
{
	(void)_mach_fork_child();
	_pthread_atfork_child();
	_malloc_fork_child();
	_pthread_atfork_child_handlers();
}

/* Body is real Libc/sys/fork.c's, unchanged -- including its real
 * behavior of running the PARENT hooks on the __fork() == -1 error
 * path, which is easy to get wrong. */
int fork(void)
{
	int ret;

	libSystem_atfork_prepare();

	ret = __fork();

	if (-1 == ret) {
		/* __fork already set errno for us */
		libSystem_atfork_parent();
		return ret;
	}

	if (0 == ret) {
		libSystem_atfork_child();
		return 0;
	}

	libSystem_atfork_parent();
	return ret;
}
