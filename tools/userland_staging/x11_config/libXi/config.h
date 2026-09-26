/* libXi-1.8.2 config.h (DAR-387). Its configure.ac checks no functions or
 * headers beyond autoconf's defaults; XORG_CHECK_MALLOC_ZERO (CA:28) leaves
 * nothing defined because Apple malloc(0) is non-NULL (see
 * x11_config/libX11/config.h). */
#include "../common_autoconf.h"
#define PACKAGE_VERSION "1.8.2"
