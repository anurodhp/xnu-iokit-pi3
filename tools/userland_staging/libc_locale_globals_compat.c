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

#define C_LOCALE_INITIALIZER	{	\
	0, XPERMANENT,			\
	{}, {}, {}, {}, {},		\
	{}, {}, {}, {}, {},		\
	OS_UNFAIR_LOCK_INIT,		\
	XMAGIC,				\
	1, 0, 0, 0, 0, 0, 1, 1, 0,	\
	NULL,				\
	&_DefaultRuneXLocale,		\
}

struct _xlocale __global_locale = C_LOCALE_INITIALIZER;
pthread_key_t __locale_key = (pthread_key_t)-1;
