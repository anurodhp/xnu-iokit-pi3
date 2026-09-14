#include "xlocale_private.h"

/* Minimal, curated subset of third_party/Libc/locale/xlocale.c -- NOT a
 * full compile of that file. Compiling xlocale.c itself pulls in
 * _modifylocale()'s real setlocale()/uselocale() machinery (collate/
 * messages/monetary/numeric per-category loaders, __detect_path_locale,
 * __get_locale_env), none of which chrony's strftime_l(..., NULL) call
 * site ever reaches -- chrony never calls setlocale/uselocale/newlocale
 * anywhere in its own source (confirmed by grep). Only the three
 * globals strftime.c/localtime.c actually reference are provided here,
 * using xlocale.c's own real C_LOCALE_INITIALIZER macro and struct
 * layout verbatim (copied from that file, not reinvented) so the ABI
 * matches exactly what the real xlocale_private.h struct expects. */

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

static struct _xlocale __c_locale = C_LOCALE_INITIALIZER;
const locale_t _c_locale = (const locale_t)&__c_locale;
struct _xlocale __global_locale = C_LOCALE_INITIALIZER;
pthread_key_t __locale_key = (pthread_key_t)-1;
