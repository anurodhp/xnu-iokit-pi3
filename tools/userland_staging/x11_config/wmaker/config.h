/* config.h for Window Maker 0.96.0 (third_party/wmaker, tag wmaker-0.96.0),
 * DAR-424. Hand-written stand-in for the autoheader output of configure.ac
 * (x11_common.sh's header explains why these ports do not run configure).
 * Every define names the configure.ac / m4 check it answers, answered for
 * this target: arm64-apple-ios14.4 SDK headers, this project's
 * libc_build/system dylibs, and the X libraries built here (libX11, libXext,
 * libXmu, libXft, fontconfig, freetype, libpng16). Shared by wrlib, WINGs
 * and src (all three include "config.h").
 *
 * configure options this corresponds to (all others at their defaults):
 *   --host=aarch64-apple-darwin  (WM_OSDEP=darwin, configure.ac:278)
 *   --prefix=/usr/X11            (paths: config-paths.h)
 *   LINGUAS unset                (no I18N, m4/wm_i18n.m4:48)
 * Libraries configure would not find here, so their features are off:
 * libXpm (built-in XPM loader instead), libjpeg, libgif, libtiff, libwebp,
 * MagickWand, libexif, pango, libXinerama, libXRes, Boehm GC.
 */

/* AC_INIT, configure.ac:37 */
#define PACKAGE "WindowMaker"
#define PACKAGE_NAME "WindowMaker"
#define PACKAGE_TARNAME "WindowMaker"
#define PACKAGE_VERSION "0.96.0"
#define PACKAGE_STRING "WindowMaker 0.96.0"
#define PACKAGE_BUGREPORT "wmaker-dev@googlegroups.com"
#define PACKAGE_URL "http://www.windowmaker.org/"
#define VERSION "0.96.0"

/* --enable-animations default (configure.ac:326), --enable-mwm-hints
 * default (configure.ac:341), --enable-xdnd default (configure.ac:561). */
#define USE_ANIMATIONS 1
#define USE_MWM_HINTS 1
#define USE_DOCK_XDND 1

/* AC_CHECK_FUNCS(gethostname select poll strcasecmp strncasecmp setsid
 * mallinfo2 mkstemp sysconf), configure.ac:389: all present in libc except
 * mallinfo2 (a glibc extension). */
#define HAVE_GETHOSTNAME 1
#define HAVE_SELECT 1
#define HAVE_POLL 1

/* DAR-431: WUtil waits in one kqueue (WINGs/handlers.c in the
 * anurodhp/wmaker iokit-port fork) instead of select(); select() remains
 * the runtime fallback (kqueue() failing, or WM_EVENT_BACKEND=select).
 * Not an autoconf symbol: it is this port's own option, default on. */
#define WM_USE_KQUEUE 1

/* DAR-434: programs are launched with posix_spawnp (src/spawn.c in the
 * anurodhp/wmaker iokit-port fork) instead of fork+exec, and their exit is
 * watched with EVFILT_PROC on the WUtil kqueue. fork+exec stays as the
 * run-time fallback (WM_SPAWN=fork). Not an autoconf symbol: this port's
 * own option, default on. posix_spawnp is libsystem_c's (build_libsystem_c.sh). */
#define WM_USE_POSIX_SPAWN 1
#define HAVE_STRCASECMP 1
#define HAVE_STRNCASECMP 1
#define HAVE_SETSID 1
#define HAVE_MKSTEMP 1
#define HAVE_SYSCONF 1
/* AC_FUNC_VPRINTF, configure.ac:387 */
#define HAVE_VPRINTF 1

/* WM_FUNC_SECURE_GETENV (m4/windowmaker.m4:247): secure_getenv is GNU-only,
 * not in this libc, so HAVE_SECURE_GETENV stays undefined. */

/* WM_FUNC_OPEN_NOFOLLOW (m4/windowmaker.m4:295): O_NOFOLLOW is in
 * <fcntl.h>, so configure defines nothing. */

/* strlcat/strlcpy found in libc without -lbsd (configure.ac:414-422).
 * configure.ac:416 and :421 both define HAVE_STRLCAT (sic) and never
 * HAVE_STRLCPY, so WUtil uses its bundled wstrlcpy (WINGs/string.c:303),
 * exactly as a real configure run would. */
#define HAVE_STRLCAT 1

/* sys/inotify.h (configure.ac:445) is Linux-only: HAVE_INOTIFY undefined. */

/* syslog.h (configure.ac:452) */
#define HAVE_SYSLOG_H 1
#define HAVE_SYSLOG 1

/* AC_HEADER_SYS_WAIT + AC_CHECK_HEADERS(fcntl.h limits.h sys/ioctl.h
 * libintl.h poll.h malloc.h ctype.h string.h strings.h), configure.ac:457.
 * The SDK has no <libintl.h> and no <malloc.h> (only <malloc/malloc.h>). */
#define HAVE_SYS_WAIT_H 1
#define HAVE_FCNTL_H 1
#define HAVE_LIMITS_H 1
#define HAVE_SYS_IOCTL_H 1
#define HAVE_POLL_H 1
#define HAVE_CTYPE_H 1
#define HAVE_STRING_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STDLIB_H 1
#define STDC_HEADERS 1

/* AC_C_CONST / AC_C_INLINE: C99 compiler, nothing to define.
 * WM_C_NORETURN (m4/wm_attributes.m4:54): clang 12 has <stdnoreturn.h>. */
#define HAVE_STDNORETURN 1
/* WM_TYPE_SIGNAL (m4/windowmaker.m4:319) */
#define RETSIGTYPE void

/* WM_I18N_XLOCALE (m4/wm_i18n.m4:181): libX11 exports _Xsetlocale
 * (nm -gU libc_build/system/libX11.dylib), so configure defines X_LOCALE. */
#define X_LOCALE 1

/* AC_CHECK_LIB([X11], [XInternAtoms]) / [XConvertCase],
 * configure.ac:519/:527: both exported by libX11.dylib. */
#define HAVE_XINTERNATOMS 1
#define HAVE_XCONVERTCASE 1

/* X extensions (m4/wm_xext_check.m4). libXext.dylib provides SHAPE and
 * MIT-SHM (x11_smoketest exercises XShmAttach). libXmu.dylib is built
 * (build_libXmu.sh). XRes, Xinerama: libraries not ported, so off.
 * RandR: --enable-randr defaults to no (configure.ac:648). */
#define USE_XSHAPE 1
#define USE_XSHM 1
#define HAVE_LIBXMU 1

/* WM_CHECK_LIBM (m4/wm_libmath.m4): M_PI and sinf/cosf are in the SDK's
 * <math.h>, provided by libsystem_m.dylib. */
#define WM_PI (M_PI)
#define HAVE_FLOAT_MATHFUNC 1

/* Graphic formats (configure.ac:733-830): only libpng16 is built here.
 * XPM uses wrlib's built-in reader (load_xpm_normalized.c). */
#define USE_PNG 1

/* WM_LIBRARY_CONSTRUCTORS (m4/wm_library_constructors.m4:63): clang
 * supports __attribute__((constructor)). */
#define WLIB_CONSTRUCTOR(func_name) __attribute__ ((constructor)) func_name

/* AH_BOTTOM, configure.ac:50 */
#include "config-paths.h"
