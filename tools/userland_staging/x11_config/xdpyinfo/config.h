/* xdpyinfo-1.4.0 config.h (DAR-387). configure.ac (CA) lines cited. Each
 * optional extension is a PKG_CHECK_MODULES for its client library followed
 * by AC_CHECK_HEADERS; only the libraries this port builds pass. */
#include "../common_autoconf.h"
#define PACKAGE_STRING "xdpyinfo 1.4.0"
#define PACKAGE_VERSION "1.4.0"
/* CA:46-50 xext: libXext stages both headers. */
#define HAVE_X11_EXTENSIONS_MULTIBUF_H 1
#define HAVE_X11_EXTENSIONS_XSHM_H 1
/* CA:52-56 x11: libX11's XKB headers. */
#define HAVE_X11_EXTENSIONS_XKB_H 1
#define HAVE_X11_XKBLIB_H 1
/* CA:93-97 xi (build_libXi.sh). */
#define HAVE_X11_EXTENSIONS_XINPUT_H 1
/* CA:99-103 xrender. */
#define HAVE_X11_EXTENSIONS_XRENDER_H 1
/* CA:135-139 xtst (build_libXtst.sh). */
#define HAVE_X11_EXTENSIONS_RECORD_H 1
/* Not built here, so their PKG_CHECK_MODULES fail and nothing is defined:
 * xxf86vm (CA:58-62), xxf86dga (CA:64-77), xf86misc (CA:79-91, off by
 * default), xcomposite (CA:105-109), xinerama (CA:111-121), dmx (CA:123-133,
 * off by default), xpresent (CA:141-145). */
