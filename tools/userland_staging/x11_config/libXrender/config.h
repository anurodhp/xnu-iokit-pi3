/* libXrender-0.9.12 config.h (DAR-383). Its configure.ac checks no functions or
 * headers beyond autoconf's defaults; the only other probe,
 * XORG_CHECK_MALLOC_ZERO (libXrender/libXrandr), leaves nothing defined
 * because Apple malloc(0) is non-NULL (see x11_config/libX11/config.h). */
#include "../common_autoconf.h"
#define PACKAGE_VERSION "0.9.12"
