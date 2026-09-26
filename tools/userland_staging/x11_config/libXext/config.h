/* libXext-1.3.7 config.h (DAR-383). configure.ac (CA) lines cited. */
#include "../common_autoconf.h"
#define PACKAGE_VERSION "1.3.7"
/* CA:31 XORG_CHECK_MALLOC_ZERO: Apple malloc(0) is non-NULL, so no
 * MALLOC_0_RETURNS_NULL (same reasoning as x11_config/libX11/config.h). */
/* CA:47 AX_GCC_BUILTIN([__builtin_popcountl]) */
#define HAVE___BUILTIN_POPCOUNTL 1
/* CA:48 AC_REPLACE_FUNCS([reallocarray]): not exported by this tree's
 * libsystem_malloc.dylib, so build_libXext.sh compiles libXext's own
 * src/reallocarray.c, as configure does when the link test fails. */
