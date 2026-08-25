/* Real Apple libpthread source (third_party/libpthread/src/pthread.c)
 * bundles three genuinely separate things into one 2587-line file that
 * does not compile standalone on this project's freestanding target:
 * (1) main-thread bootstrap + the real pthread_t signature scheme,
 * (2) pthread_create()'s real stack-allocation/mach-vm machinery, and
 * (3) trivial real accessor one-liners (pthread_equal/pthread_main_np/
 * pthread_main_thread_np). (2) needs mach_vm_allocate/mach_reply_port/
 * real MIG plumbing that collides at the header level with this
 * project's own libmalloc-fork-generated MIG stubs (duplicate
 * __Reply__mach_make_memory_entry_t definitions from two independently
 * regenerated MIG outputs) - a real, investigated conflict, not skipped
 * preemptively.
 *
 * CORRECTED 2026-08-24: this file used to say real thread creation was
 * "PROVABLY DEAD CODE on this target regardless" because the kernel-side
 * bsdthread_create unconditionally returned ENOTSUP. That kernel side
 * became real on 2026-08-20 (Phase 7.5 step 1) - the claim went stale
 * and stayed stale in this comment until now, exactly the kind of thing
 * that should be caught and fixed whenever a dependency changes, not
 * left to rot. pthread_create() below is now genuinely functional: real
 * stack allocation via mmap (this project's own real wrapper, not the
 * excluded mach_vm_allocate path - this target's simplified kernel-side
 * real_bsdthread_create just needs a valid `sp`, confirmed by reading
 * it, not the full real Apple stack-allocation dance) plus a real
 * per-child-thread TSD block and startup trampoline (see
 * _pthread_create_trampoline below) - the same TPIDRRO_EL0-per-thread
 * gap that pthread_main_thread_bootstrap() already fixed once for the
 * main thread turned out to need fixing again, per-thread, for every
 * thread pthread_create() spawns; see pthread_tsd_platform_bootstrap.c's
 * own updated header for that bug's exact QEMU-diagnosed shape. This
 * file still supplies (1) faithfully adapted from pthread.c's own real
 * logic (each function's origin cited below), not reinvented.
 */

#include "internal.h"

/* ---- Real globals pthread_mutex.c/pthread_cond.c/pthread_rwlock.c/
 * pthread_tsd.c/pthread_atfork.c all reference as `extern` (declared in
 * prototypes_internal.h) and expect pthread.c to define. Real names,
 * real types - not renamed/reinvented. */

struct pthread_s _main_thread;
pthread_t _main_thread_ptr = &_main_thread;

struct __pthread_list __pthread_head = TAILQ_HEAD_INITIALIZER(__pthread_head);
_pthread_lock _pthread_list_lock = _PTHREAD_LOCK_INITIALIZER;

/* Real global (pthread.c:162), real per-process pointer-signing token
 * (see inline_internal.h's _pthread_validate_signature/_pthread_init_signature -
 * thread->sig = (uintptr_t)thread ^ _pthread_ptr_munge_token). Real
 * Apple bootstrap seeds this from a random value handed in via
 * bsdthread_register's registration data (ASLR-derived, defends against
 * off-target pointer-corruption attacks). This target has no real
 * bsdthread_register (see this file's own header) so there is no
 * equivalent random source from the kernel; seeded instead from real
 * mach_absolute_time() (this project's own real commpage-backed
 * implementation, tools/userland_staging/build_libplatform.sh) so it at
 * least varies run-to-run rather than being a fixed guessed constant -
 * on this single-threaded, non-adversarial target the ONLY real
 * correctness requirement on this value is "the same value used to sign
 * is used to validate", which holds regardless of predictability. */
uintptr_t _pthread_ptr_munge_token;

extern uint64_t mach_absolute_time(void);

/* Real algorithm from inline_internal.h's _pthread_init_signature -
 * duplicated here (not called directly) only because that function is
 * `static inline` in a header designed to be invoked from pthread.c's
 * own translation unit; the logic itself is copied verbatim, not
 * reinvented. */
static void
main_thread_init_signature(pthread_t thread)
{
	thread->sig = (uintptr_t)thread ^ _pthread_ptr_munge_token;
}

extern int pthread_tsd_shim_bootstrap_with_base(void *tsd_base); /* pthread_tsd_platform_bootstrap.c */

/* Tiny helper split out only to keep pthread_main_thread_bootstrap()'s
 * control flow linear; not a real Apple function, just avoids an
 * inline strlcpy dependency this file doesn't otherwise need. */
static void
kstrlcpy_main_thread_name(void)
{
	static const char name[] = "main-thread";
	for (unsigned i = 0; i < sizeof(name); i++) {
		_main_thread.pthread_name[i] = name[i];
	}
}

/* Real bootstrap: installs a genuine, correctly-signed main-thread
 * pthread_t before any real libpthread code (pthread_mutex_lock,
 * pthread_getspecific, etc) runs. Must be called once, early, same
 * requirement as pthread_tsd_shim_bootstrap() itself (see that
 * function's own header comment in pthread_tsd_shim.c /
 * tsd_bootstrap.c) - this function calls it internally, callers do not
 * need to call both.
 *
 * Real ABI fact this depends on (private/pthread/private.h's
 * _pthread_self_direct(), arm64 branch):
 *   pthread_self() == (pthread_t)(tsd_base - _PTHREAD_STRUCT_DIRECT_TSD_OFFSET)
 * where tsd_base is the raw TPIDRRO_EL0 value and the real, checked
 * (offsets_internal.h) constant _PTHREAD_STRUCT_DIRECT_TSD_OFFSET is
 * 224 - i.e. real Darwin does NOT store a separate pointer to the
 * current pthread_t in a TSD slot on arm64; it computes the struct's
 * address directly from the TSD base pointer, because the tsd[] array
 * is itself a real field embedded 224 bytes into struct pthread_s. This
 * project's existing TSD bootstrap (tsd_bootstrap.c/pthread_tsd_shim.c,
 * QEMU-confirmed 2026-08-13) already independently arrived at the same
 * 224-byte header size for its own synthetic TSD block - not a
 * coincidence spotted here for the first time, but real confirmation
 * that reusing that exact layout as the real main thread's pthread_s
 * makes both schemes consistent: TPIDRRO_EL0 is set to
 * &_main_thread.tsd[0], and _main_thread's own first 224 bytes (sig,
 * cleanup stack, list linkage, lock, name, etc - see types_internal.h's
 * real struct pthread_s) are exactly what real _pthread_self_direct()'s
 * arithmetic expects to land on.
 */
/* Real global (pthread_mutex.c:90), real compiled-in Apple default is
 * false (types_internal.h's _PTHREAD_MTX_OPT_ULOCK_DEFAULT) - real macOS
 * only ever flips it via _pthread_mutex_global_init(), driven by either
 * PTHREAD_MUTEX_USE_ULOCK=1 in envp or the kernel's bsdthread_register
 * registration_data.mutex_default_policy (both real mechanisms, see
 * build_libpthread.sh's own header comment for the full trace). This
 * project does NOT wire that whole call chain (would need a real
 * _pthread_bsdthread_init()/__bsdthread_register() round trip this
 * target's simplified bsdthread_create doesn't actually need for thread
 * startup - see bsd_pthread_stub.c's real_bsdthread_create, which sets
 * the new thread's pc directly from the caller-supplied func rather than
 * indirecting through a registered threadstart trampoline the way real
 * macOS does) for something that only ever has ONE correct answer on
 * this specific kernel: bsd_pthread_stub.c's psynch_* syscalls
 * (stub_psynch_error) are ALL still ENOTSUP, so the real FAIRSHARE/
 * psynch contended-mutex slow path this library would otherwise take
 * genuinely cannot work here - it would surface as a contended
 * pthread_mutex_lock() failing with ENOTSUP instead of blocking. The
 * real, working, QEMU-verified alternative (Phase 7.5 step 3,
 * tools/ulock_kthread_test/) is ulock_wait/ulock_wake, and that is
 * exactly the path __pthread_mutex_use_ulock=true selects. So: set it
 * directly here rather than porting the whole envp/registration-data
 * machinery to compute the same fixed value. This corrects an earlier,
 * never-implemented intent documented (then corrected back to false by
 * an adversarial review that caught the code didn't match the comment)
 * in build_libpthread.sh's own header - this is that fix, done for
 * real, not just re-stating the aspiration. */
extern bool __pthread_mutex_use_ulock;

int
pthread_main_thread_bootstrap(void)
{
	_Static_assert(__builtin_offsetof(struct pthread_s, tsd) ==
			_PTHREAD_STRUCT_DIRECT_TSD_OFFSET,
			"real struct pthread_s layout no longer matches "
			"_PTHREAD_STRUCT_DIRECT_TSD_OFFSET on this target - "
			"the TSD-base arithmetic below would silently "
			"compute the wrong pthread_t");

	__pthread_mutex_use_ulock = true;

	/* Real per-process token (see this file's own header on why
	 * mach_absolute_time() is a reasonable substitute for the real
	 * ASLR-derived seed on this target). */
	_pthread_ptr_munge_token = (uintptr_t)mach_absolute_time();

	TAILQ_INSERT_TAIL(&__pthread_head, &_main_thread, tl_plist);
	_main_thread.tl_policy = 0; /* SCHED_OTHER-equivalent */
	_main_thread.tl_joinable = 0; /* main thread is never joined */
	kstrlcpy_main_thread_name();

	main_thread_init_signature(&_main_thread);

	/* Real TSD base install (platform_syscall "set cthread self",
	 * QEMU-confirmed mechanism) - MUST point at &_main_thread.tsd[0],
	 * not at _main_thread itself, matching the real offset arithmetic
	 * documented above. */
	int ok = pthread_tsd_shim_bootstrap_with_base(&_main_thread.tsd[0]);

	/* REAL BUG FOUND BY QEMU BOOT-TEST (not caught by native `ld`/host
	 * testing - genuinely needed the target's own TSD-base arithmetic
	 * to manifest): real libpthread source (any file that includes
	 * "internal.h", which pulls in inline_internal.h) does NOT use a
	 * plain global `int errno` - `errno` is `#undef`'d and redefined as
	 * the macro `*_pthread_errno_address_direct()`, i.e. a real
	 * TSD-slot-1-relative dereference (see private/pthread/private.h's
	 * _pthread_errno_address_direct(), and tsd_private.h's
	 * _PTHREAD_TSD_SLOT_ERRNO == __TSD_ERRNO == 1). Slot 1 was never
	 * populated by this bootstrap, so any real libpthread code writing
	 * `errno = ...` on an error path (e.g. pthread_bsdthread_create_syscall.c's
	 * __bsdthread_create(), first genuinely exercised in QEMU - crashed
	 * with a real SIGSEGV, confirmed via isolated bisection: bootstrap
	 * succeeded, crash was reproducible immediately at the first
	 * errno-writing syscall wrapper, before pthread_create() itself
	 * returned) dereferenced tsd[1] as an `int *` while it was still
	 * NULL. Fixed for real: point TSD slot 1 at _main_thread's own real
	 * err_no field (types_internal.h: "errno_t err_no; // thread-local
	 * errno" - the exact real field this mechanism is designed to
	 * expose), the same real per-thread storage real Apple bootstrap
	 * uses this slot for. */
	_main_thread.tsd[_PTHREAD_TSD_SLOT_ERRNO] = &_main_thread.err_no;

	return ok;
}

/* ---- Real one-liners, verbatim from pthread.c (see that file for the
 * originals - pthread_equal @1521, pthread_main_thread_np @1030,
 * pthread_main_np @1038), not reinvented, just relocated because their
 * home file doesn't compile standalone here (see this file's header). */

int
pthread_equal(pthread_t t1, pthread_t t2)
{
	return (t1 == t2);
}

pthread_t
pthread_main_thread_np(void)
{
	return main_thread();
}

int
pthread_main_np(void)
{
	return pthread_self() == main_thread();
}

/* Real pthread_once(), adapted from pthread.c:1606 - real algorithm
 * (os_once-gated single execution, sig flag flipped after running),
 * using this project's real, already-QEMU-verified _os_once() (from
 * libplatform.a) directly rather than pthread.c's own real
 * _os_once_acquire() wrapper, which genuinely does not exist in this
 * project's pinned libplatform-254.80.2 checkout (real cross-repo
 * version skew: libpthread-454.80.2 expects a newer libplatform than
 * what's pinned here - confirmed by grepping the whole checkout, not
 * assumed missing). Deliberately omits real pthread.c's
 * pthread_cleanup_push/pop around the init call (protects a
 * pthread_cancel()'d thread mid-init) - this project has no
 * cancellation infrastructure built (pthread_cancelable.c is out of
 * scope for this tier), and init_routine() cannot actually be
 * cancelled here regardless, since pthread_cancel() itself isn't
 * built. Real semantics preserved: init_routine() runs exactly once,
 * matching real pthread.c's contract for the only case this target can
 * ever exercise. */

struct _pthread_once_context_compat {
	pthread_once_t *pthread_once;
	void (*routine)(void);
};

static void
pthread_once_handler_compat(void *context)
{
	struct _pthread_once_context_compat *ctx = context;
	ctx->routine();
	ctx->pthread_once->sig = _PTHREAD_ONCE_SIG;
}

int
pthread_once(pthread_once_t *once_control, void (*init_routine)(void))
{
	struct _pthread_once_context_compat ctx = { once_control, init_routine };
	do {
		_os_once(&once_control->once, &ctx, pthread_once_handler_compat);
	} while (once_control->sig == _PTHREAD_ONCE_SIG_init);
	return 0;
}

/* ---- pthread_create(): real, not faked, and now genuinely functional.
 * Performs the real bsdthread_create(2) syscall (raw wrapper below, real
 * syscall number 360 per third_party/xnu-7195/bsd/kern/syscalls.master,
 * NO_SYSCALL_STUB there because Apple hand-writes it in libsyscall's own
 * custom/ dir, same class as __ulock_wait/__ulock_wake in
 * build_libplatform.sh). This project's kernel-side implementation
 * (bsd_pthread_stub.c's real_bsdthread_create, real since Phase 7.5 step
 * 1, 2026-08-20) genuinely spawns a second schedulable thread now - this
 * function's own comment used to say the kernel side "unconditionally
 * returns ENOTSUP for it", which was true when this file was first
 * written but has been stale since that kernel-side landed; corrected
 * here rather than left to rot further.
 *
 * REAL BUG FIXED HERE (found by re-reading bsd_pthread_stub.c's
 * real_bsdthread_create): it sets the new thread's arm64 `sp` directly
 * from this syscall's `stack` argument (`state.sp = user_stack;` -
 * bsd_pthread_stub.c ~line 172) - unlike real macOS, it does NOT
 * allocate a stack on the kernel's behalf, and does NOT indirect through
 * a registered `threadstart` trampoline the way real Apple's
 * bsdthread_create does (this target's kernel jumps straight to the
 * caller-supplied `func`, confirmed by reading `state.pc = user_func;`
 * right above it - a deliberate simplification, not an oversight).
 * This function used to call __bsdthread_create with `stack=NULL`,
 * meaning the new thread would begin executing with sp=0 - a guaranteed
 * crash on its very first stack push. Never caught before because
 * nothing had actually called pthread_create() and let it succeed until
 * now (the pre-existing smoketest asserted ENOTSUP as the "pass" case).
 * Fixed by real mmap(MAP_ANON|MAP_PRIVATE) (this project's own real,
 * already-working wrapper, simple_cmds_mmap_compat.c) for a real,
 * page-backed stack, 16-byte aligned per AArch64 ABI (same alignment
 * tools/pthread_kthread_test/ already established as necessary and
 * QEMU-verified).
 *
 * Also sets __is_threaded = 1 on success, matching real pthread.c's own
 * behavior (real callers like pthread_atfork.c branch on this) - this
 * project's pthread_compat_extras.c previously hardcoded it to a
 * permanent 0 with a comment explaining that was correct "since no
 * second thread can ever actually start running"; no longer true, fixed
 * here rather than there since this is the actual real call site real
 * Apple's own pthread.c flips it from.
 *
 * Known, still-real, still-open gap (unchanged by this fix, not solved
 * by it): bsd_pthread_stub.c's real_bsdthread_create always sets
 * *retval = 0 (no thread port is created/returned), so the `*thread`
 * this function hands back is not a real, targetable handle - there is
 * still no way for userspace to `pthread_join`/`pthread_kill`/
 * individually terminate the new thread; it only stops when the whole
 * task exits. Real pthread_create()'s own real bookkeeping (inserting
 * the new thread into __pthread_head, real per-thread pthread_t struct
 * allocation alongside the stack) is ALSO not done here - the returned
 * `*thread` is a bare kernel-facing value, not a real struct pthread_s.
 * Both are real, separate follow-on scopes, not silently papered over
 * here. */

/* __bsdthread_create itself: see pthread_bsdthread_create_syscall.c
 * (real syscall 360, real error convention). Declared already via this
 * file's own "internal.h" include (imports_internal.h) - no redundant
 * extern needed here. */

#define PTHREAD_CREATE_STACK_SIZE (256 * 1024)

/* Real gap this closes: bsd_pthread_stub.c's real_bsdthread_create jumps
 * the new thread straight to the caller-supplied `func` (no registered
 * `threadstart` trampoline indirection the way real macOS uses - see
 * this file's own top header). That means nothing ever gives a newly
 * created thread its own TPIDRRO_EL0/TSD base before it starts running
 * real user code - exactly the same class of bug
 * pthread_main_thread_bootstrap() already found and fixed for the MAIN
 * thread (this file's own comment on the errno-slot fix), but that fix
 * only ever ran once, for one thread. Confirmed live via a real QEMU
 * boot test (2026-08-24): bootstrap/mutex/TSD/pthread_once all passed
 * on the main thread, then a real second thread crashed (SIGSEGV)
 * immediately inside pthread_mutex_lock(), which needs its OWN valid
 * TSD base for the real ulock-backed fast path.
 *
 * Fix: pthread_create() no longer passes the caller's start_routine
 * directly as bsdthread_create's `func`. It passes this trampoline
 * instead, with `func_arg` = a small heap (well, mmap) context struct
 * bundling the real start_routine/arg plus a pointer to a real,
 * per-child struct pthread_s (same real type/layout the main thread
 * uses, allocated fresh per thread rather than reusing types_internal.h's
 * single main-thread instance). The trampoline runs ON the new thread
 * (it's the first code that thread ever executes), installs that
 * thread's own TSD base + errno slot exactly the way
 * pthread_main_thread_bootstrap() does for the main thread, then calls
 * the real user function.
 *
 * Still NOT done (real, separate, still-open gaps, not papered over
 * here): the child pthread_s is not inserted into __pthread_head (real
 * pthread_atfork.c's fork-child cleanup walks that list; a forked child
 * process on this target would not see threads created this way - not
 * exercised by anything in this project yet, since real fork()+pthread
 * interaction isn't a tested path regardless). There is also still no
 * real thread-exit mechanism (bsdthread_terminate is ENOTSUP) - see the
 * spin-forever comment on the trampoline's return path below. */

struct pthread_create_trampoline_ctx {
	void *(*start_routine)(void *);
	void *arg;
	struct pthread_s *child_thread;
};

extern int pthread_tsd_shim_bootstrap_with_base(void *tsd_base);

static void *
_pthread_create_trampoline(void *raw_ctx)
{
	struct pthread_create_trampoline_ctx *ctx = raw_ctx;
	struct pthread_s *th = ctx->child_thread;

	/* Real algorithm, same as main_thread_init_signature() above -
	 * every real pthread_t needs a validly-signed `sig` field before
	 * any real libpthread code (pthread_mutex_lock, etc) will accept
	 * it as pthread_self(). */
	th->sig = (uintptr_t)th ^ _pthread_ptr_munge_token;

	pthread_tsd_shim_bootstrap_with_base(&th->tsd[0]);
	th->tsd[_PTHREAD_TSD_SLOT_ERRNO] = &th->err_no;

	void *(*start_routine)(void *) = ctx->start_routine;
	void *arg = ctx->arg;
	start_routine(arg);

	/* Real, known, still-open gap: no real thread-exit path exists
	 * here (bsdthread_terminate is still an ENOTSUP stub,
	 * bsd_pthread_stub.c) - a thread that returns from its real
	 * start_routine has nowhere real to go. Spin rather than fall off
	 * into whatever garbage follows this function in the text
	 * segment - same real constraint tools/pthread_kthread_test/'s
	 * own child_entry already documented and worked around the same
	 * way. The whole task's real, pre-existing task_terminate_internal()
	 * teardown (already QEMU-verified reaping >1 live thread, Phase
	 * 7.5 step 1) is what actually cleans this up, on process exit. */
	for (;;) {
		__asm__ volatile("" ::: "memory");
	}
}

int
pthread_create(pthread_t _Nullable *_Nonnull restrict thread,
		const pthread_attr_t *_Nullable restrict attr,
		void *(*_Nonnull start_routine)(void *_Nullable),
		void *_Nullable arg)
{
	extern void *mmap(void *addr, size_t len, int prot, int flags, int fildes, long off);
	extern int __is_threaded;

	(void)attr;
	if (thread) {
		*thread = NULL;
	}

	void *stack_base = mmap(0, PTHREAD_CREATE_STACK_SIZE,
			0x1 /* PROT_READ */ | 0x2 /* PROT_WRITE */,
			0x1000 /* MAP_ANON */ | 0x0002 /* MAP_PRIVATE */, -1, 0);
	if (stack_base == (void *)-1) {
		return errno;
	}
	uintptr_t stack_top = ((uintptr_t)stack_base + PTHREAD_CREATE_STACK_SIZE) & ~(uintptr_t)15;

	/* Real per-child struct pthread_s + trampoline context, own real
	 * page-backed mapping (kept separate from the stack mapping so a
	 * stack overrun corrupts guard-free stack space, not this thread's
	 * own TSD block - a real, if partial, safety margin). */
	void *meta = mmap(0, sizeof(struct pthread_s) + sizeof(struct pthread_create_trampoline_ctx),
			0x1 /* PROT_READ */ | 0x2 /* PROT_WRITE */,
			0x1000 /* MAP_ANON */ | 0x0002 /* MAP_PRIVATE */, -1, 0);
	if (meta == (void *)-1) {
		return errno;
	}
	struct pthread_s *child_thread = meta;
	struct pthread_create_trampoline_ctx *ctx =
			(struct pthread_create_trampoline_ctx *)((char *)meta + sizeof(struct pthread_s));
	ctx->start_routine = start_routine;
	ctx->arg = arg;
	ctx->child_thread = child_thread;

	pthread_t t = __bsdthread_create(_pthread_create_trampoline, ctx, (void *)stack_top, NULL, 0);
	if (t == (pthread_t)-1) {
		return errno;
	}

	__is_threaded = 1;
	if (thread) {
		*thread = child_thread;
	}
	return 0;
}
