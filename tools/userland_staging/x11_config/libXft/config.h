/* libXft-2.3.9 config.h (DAR-384). Its configure.ac checks no functions or
 * headers of its own: LT_INIT/XORG_DEFAULT_OPTIONS (configure.ac:39-45)
 * give the autoconf defaults in common_autoconf.h, and the three
 * PKG_CHECK_MODULES (xrender/x11, freetype2, fontconfig; :67-73) only set
 * compiler and linker flags. XFT_MAJOR/MINOR/REVISION (:50-59) go into
 * the generated Xft.h, not here (build_libXft.sh). */
#include "../common_autoconf.h"
#define PACKAGE_VERSION "2.3.9"   /* configure.ac:29 AC_INIT */
