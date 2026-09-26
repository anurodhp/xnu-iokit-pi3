/* libXmu-1.3.1 config.h (DAR-383). configure.ac (CA) lines cited. */
#include "../common_autoconf.h"
#define PACKAGE_VERSION "1.3.1"
/* CA:37 AC_CHECK_FUNCS([uname]) -- exported by libsystem_c.dylib. */
#define HAVE_UNAME 1
/* CA:38 AC_REPLACE_FUNCS([reallocarray]): not exported from this tree's
 * libsystem_malloc.dylib, so build_libXmu.sh compiles libXmu's own
 * src/reallocarray.c, as configure does when the link test fails. */
