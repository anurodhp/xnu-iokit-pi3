/* libXt-1.3.1 config.h (DAR-383). configure.ac (CA) lines cited. */
#include "../common_autoconf.h"
#define PACKAGE_VERSION "1.3.1"
/* CA:48 XORG_CHECK_MALLOC_ZERO: Apple malloc(0) is non-NULL -> no
 * XTMALLOC_BC / MALLOC_0_RETURNS_NULL (see x11_config/libX11/config.h). */
/* CA:59 alloca.h exists in the iOS 14.4 SDK. */
#define INCLUDE_ALLOCA_H 1
/* CA:62 AC_CHECK_FUNCS([reallocarray]) (check only, no replacement):
 * not exported from libsystem_malloc.dylib in this tree, so left
 * undefined and Alloc.c uses its own overflow-checked realloc path. */
/* CA:98-111: darwin11+ -> AC_CHECK_FUNC(poll); poll() is syscall #230,
 * exported by libsystem_kernel.dylib. */
#define USE_POLL 1
/* CA:121-129, 131-138: --enable-xkb and --enable-const default yes. */
#define XKB 1
#define _CONST_X_STRING 1
/* CA:140-145 geo-tattler: default no. CA:150-154 unit tests: off. */
/* CA:168-174 AX_DEFINE_DIR(appdefaultdir) for --prefix=/usr/X11. */
#define appdefaultdir "/usr/X11/share/X11/app-defaults"
