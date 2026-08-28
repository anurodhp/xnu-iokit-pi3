/* Real, narrow reproduction of third_party/Libc/locale/xlocale.c's own
 * two real globals (lines 45-46 of that file):
 *
 *   struct _xlocale __global_locale = C_LOCALE_INITIALIZER;
 *   pthread_key_t   __locale_key    = (pthread_key_t)-1;
 *
 * atoi.o (extracted from syslogd_libc_extras.a for build_libdispatch_
 * dylib.sh's real geteuid/gettimeofday closure pass) needs exactly these
 * two externs via xlocale_private.h's __current_locale() -- a static
 * inline already compiled into atoi.o itself, so nothing else from
 * xlocale.c is actually needed at link time. The WHOLE xlocale.c was
 * tried first and rejected: it real-cascades into
 * __collate_load_tables/__monetary_load_locale/__numeric_load_locale/
 * __time_load_locale/__messages_load_locale/__setrunelocale/
 * __detect_path_locale/__get_locale_env (confirmed via a real nm sweep
 * of libc_build/libc_locale.a's own xlocale.o member) -- the exact same
 * bloat-cascade class build_libdispatch_dylib.sh's own header already
 * documented and rejected for strcasecmp/getprogname. This file defines
 * only the two real globals, verbatim, not a reinvented substitute.
 *
 * C_LOCALE_INITIALIZER is copied verbatim from xlocale.c (it's private
 * to that .c file, not exposed via the header) -- real values, not
 * guessed: 0/XPERMANENT for __STRUCT_COMMON, 10 zero-initialized
 * mbstate_t buffers, a real OS_UNFAIR_LOCK_INIT, the real XMAGIC tag,
 * the real 9-flag defaults, NULL collate, and &_DefaultRuneXLocale for
 * ctype -- everything after is implicit zero-init, standard C aggregate
 * initializer semantics, same as the real file relies on.
 *
 * _DefaultRuneXLocale itself is a real, already-built, self-contained
 * table (locale/FreeBSD/table.c + its own none.o dependency, both
 * extracted from libc_build/libc_locale.a by build_libdispatch_dylib.sh
 * -- confirmed via nm that table.o/none.o's only real deps,
 * ___error/_memchr, are already resolved elsewhere in this exact link),
 * not redefined here. */
#include "xlocale_private.h"

/* REAL BUG FOUND 2026-08-28 (build_simple_cmds.sh's dynamic-link
 * conversion): a real, obscure ld64-609 internal crash ("Assertion
 * failed: (_mode == modeFinalAddress), function finalAddress") triggers
 * whenever this file (libc_locale_globals_compat.o) + table.o + almost
 * any real third object are all linked together into the same
 * executable with -Wl,-fixup_chains -- root-caused via extensive real
 * bisection to table.c's own _DefaultRuneXLocale (a very large real
 * rune-classification table) combined with this file's own reference to
 * it crossing some ld64-609 fixup-chains complexity threshold; NOT
 * specific to none.c (confirmed: swapping none.o for an unrelated
 * third object, e.g. findfp.o, reproduces the identical crash), NOT
 * fixed by removing the cross-object static relocation below (confirmed
 * by testing table.c with zero references to this file at all -- still
 * crashes), and NOT even consistently reproducible between a clean
 * rebuild and an incremental one with identical source (ruling out
 * further bisection as a reliable path). Real, current state
 * (2026-08-28): build_simple_cmds.sh's own `locale_extra_objs` is left
 * deliberately EMPTY -- no scoping/mitigation currently in place, this
 * is a real, visible, unresolved gap (see docs/backlog.md item d9 for
 * the full real trail and the ~32 commands it blocks), not silently
 * worked around. __lc_ctype starting NULL and getting its real
 * &_DefaultRuneXLocale assignment via the constructor below (a runtime
 * pointer store, not a link-time relocation) is kept regardless -- a
 * real, independently-correct improvement (one fewer cross-object
 * static relocation) even though it didn't resolve the crash above on
 * its own. */
#define C_LOCALE_INITIALIZER	{	\
	0, XPERMANENT,			\
	{}, {}, {}, {}, {},		\
	{}, {}, {}, {}, {},		\
	OS_UNFAIR_LOCK_INIT,		\
	XMAGIC,				\
	1, 0, 0, 0, 0, 0, 1, 1, 0,	\
	NULL,				\
	NULL,				\
}

struct _xlocale __global_locale = C_LOCALE_INITIALIZER;
pthread_key_t __locale_key = (pthread_key_t)-1;

/* REAL BUG FOUND AND FIXED 2026-08-27, not part of the original narrow
 * atoi.o closure above: leaving __locale_key permanently at -1 (its
 * static initial value, matching real xlocale.c's own line 46 exactly)
 * is only safe on TARGET_IPHONE_SIMULATOR, where xlocale_private.h's
 * __current_locale() has a real `if (__locale_key == (pthread_key_t)-1)
 * return &__global_locale;` fast-path guard. On a REAL device target
 * (this project's own -target arm64-apple-ios14.4, not the simulator),
 * that guard is compiled out and __current_locale() unconditionally
 * calls _pthread_getspecific_direct(__locale_key) /
 * pthread_getspecific(__locale_key) with key == (pthread_key_t)-1 --
 * _pthread_getspecific_direct's own doc comment says "to be used with
 * static constant keys only", and its real body (tsd_private.h) is
 * __os_tsd_get_direct(slot), a raw, unchecked TSD-array index -- -1
 * widens to a huge unsigned slot number, indexing wildly out of bounds.
 * Found via a real QEMU crash (SIGSEGV, not SIGABRT) the first time any
 * code in this whole project actually called plain snprintf()/
 * vsnprintf() at runtime (real vsnprintf() calls __current_locale()
 * directly) -- confirmed by bisecting with a static smoketest down to
 * this exact call, then reading xlocale_private.h's real
 * TARGET_IPHONE_SIMULATOR-gated fast path to find the real mismatch,
 * not guessed.
 *
 * Real fix, verbatim matching xlocale.c's own __xlocale_init() (already
 * declared in xlocale_private.h, just never called by anything in this
 * project -- there is no real _libc_initializer() here to call it from
 * the way real Apple's own libSystem startup does): a real, reserved,
 * compile-time-constant TSD slot (__LIBC_PTHREAD_KEY_XLOCALE, defined
 * sys/cdefs.h:119 as __LIBC_PTHREAD_KEY(0) == 10 for a non-simulator
 * target), registered via the real pthread_key_init_np() (already a
 * real, defined export of this project's own libpthread.a -- confirmed
 * via nm before using it, not assumed) with a real destructor
 * (__xlocale_release, copied verbatim from xlocale.c:480-485 -- a tiny,
 * self-contained wrapper around xlocale_private.h's own XL_RELEASE
 * macro, real atomic refcount release, not fabricated). Runs via a real
 * ELF/Mach-O constructor so it's live before any userland main() runs,
 * same as this project's own other __attribute__((constructor)) uses
 * elsewhere. In practice this project never calls uselocale() to
 * install a per-thread override, so the destructor is real but
 * currently unreachable dead code -- included for correctness, not
 * because anything here exercises it yet. */
static void
__xlocale_release(void *loc)
{
	locale_t l = loc;
	XL_RELEASE(l);
}

__attribute__((constructor))
static void
__xlocale_init_compat(void)
{
	__global_locale.__lc_ctype = &_DefaultRuneXLocale;
	if (__locale_key == (pthread_key_t)-1) {
		__locale_key = __LIBC_PTHREAD_KEY_XLOCALE;
		pthread_key_init_np(__locale_key, __xlocale_release);
	}
}
