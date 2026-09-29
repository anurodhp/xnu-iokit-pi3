/* libXpm-3.5.19 config.h. configure.ac (CA) lines cited. */
#include "../common_autoconf.h"
#define PACKAGE_VERSION "3.5.19"
/* CA:24 AC_CHECK_FUNCS([strlcat]) -- exported by libsystem_c.dylib
 * (parse.c:52 then calls it instead of its own fallback). */
#define HAVE_STRLCAT 1
/* CA:25 AC_CHECK_FUNC([fork],[], AC_DEFINE(NO_ZPIPE)): fork exists, but
 * CA:70-96 --enable-open-zfile defaults to yes only when gzip is found
 * (XPM_PATH_PROG gzip is an AC_MSG_ERROR otherwise); this image ships no
 * gzip, so the equivalent of --disable-open-zfile is chosen (CA:79-80
 * defines NO_ZPIPE 1), and STAT_ZFILE (CA:100-105) then stays undefined
 * because it defaults to $OPEN_ZFILE. xdm only reads plain .xpm files. */
#define NO_ZPIPE 1
/* CA:31 AC_SEARCH_LIBS([gettext], [intl]) -> USE_GETTEXT: no libintl in
 * this tree, so undefined (the messages stay English). */
