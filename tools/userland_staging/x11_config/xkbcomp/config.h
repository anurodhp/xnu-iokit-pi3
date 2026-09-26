/* xkbcomp-1.5.0 config.h (DAR-387). configure.ac (CA) lines cited. */
#include "../common_autoconf.h"
#define PACKAGE_VERSION "1.5.0"
/* CA:53 AC_CHECK_FUNCS([asprintf reallocarray recallocarray]).
 * asprintf is exported by libsystem_c.dylib. reallocarray and
 * recallocarray are exported by no dylib here, so they stay undefined and
 * utils.h:72-78 uses xkbcomp's own fallbacks (utils.c:36 uRecalloc). */
#define HAVE_ASPRINTF 1
