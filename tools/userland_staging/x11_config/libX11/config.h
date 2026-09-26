/* libX11-1.8.13 config.h (DAR-383). configure.ac (CA) and xtrans-1.6.0's
 * xtrans.m4 (XT) line numbers cited. "exported" below means a real export
 * of a dylib under tools/userland_staging/libc_build/system/, checked with
 * nm -gU when this file was written. */
#include "../common_autoconf.h"
#define PACKAGE_VERSION "1.8.13"
#define PACKAGE_STRING "libX11 1.8.13"

/* CA:98 XTRANS_CONNECTION_FLAGS */
#include "../xtrans_flags.h"

/* ---- CA:101-104 AC_SEARCH_LIBS(dlopen) -> in libc on Darwin
 * (libdyld.dylib exports _dlopen/_dlsym, real dyld-940). */
#define HAVE_DLOPEN 1
#define HAVE_DLFCN_H 1
/* CA:115-130 --enable-loadable-i18n defaults to no: the lc/im/om modules
 * are linked into libX11 itself (USE_DYNAMIC_LC stays undefined). */
/* CA:133-142 --enable-loadable-xcursor defaults to HAVE_LOADABLE_MODULES
 * (yes). NOTE: src/CrGlCur.c:40-47 only special-cases cygwin, so on any
 * Darwin build (this one, and XQuartz's) the name it dlopen()s is
 * "libXcursor.so.1", which never exists -- the dlopen fails and Xlib
 * falls back to core-protocol cursors, exactly as upstream does on macOS. */
#define USE_DYNAMIC_XCURSOR 1
/* CA:144-152 --enable-thread-safety-constructor defaults to yes. */
#define USE_THREAD_SAFETY_CONSTRUCTOR 1

/* CA:154 AC_CHECK_HEADERS([sys/filio.h sys/select.h sys/ioctl.h sys/socket.h]) --
 * all four exist in the iOS 14.4 SDK. */
#define HAVE_SYS_FILIO_H 1
#define HAVE_SYS_SELECT_H 1
#define HAVE_SYS_IOCTL_H 1
#define HAVE_SYS_SOCKET_H 1
/* CA:157 AX_GCC_BUILTIN([__builtin_popcountl]) -- clang has it. */
#define HAVE___BUILTIN_POPCOUNTL 1
/* CA:160 AC_CHECK_FUNCS([strtol seteuid]) -- libsystem_c / libsystem_kernel. */
#define HAVE_STRTOL 1
#define HAVE_SETEUID 1
/* CA:161 AC_REPLACE_FUNCS([reallocarray]): no dylib in this tree exports
 * it from its real Apple home (libsystem_malloc -- the SDK's
 * libsystem_malloc.tbd lists it; ours does not), so, exactly as configure
 * does when the link test fails, HAVE_REALLOCARRAY stays undefined and
 * build_libX11.sh compiles libX11's own src/reallocarray.c. */
/* CA:164-169 issetugid() declared (SDK unistd.h) and exported
 * (libsystem_c.dylib) -> HASSETUGID, and XLOCALEDIR is honoured
 * (NO_XLOCALEDIR stays undefined, CA:292-299). */
#define HASSETUGID 1
/* CA:170-171 getresuid(): not on Darwin. HASGETRESUID undefined. */
/* CA:173 AC_CHECK_FUNC([shmat]): no dylib here exports shmat, so HAS_SHM
 * stays undefined; Font.c's XF86BigFont path then uses the non-shm reply,
 * which it supports. */
/* CA:183-191 --with-launchd=auto probes the BUILD host's PATH for a
 * `launchd` program. Left undefined on purpose: HAVE_LAUNCHD/TRANS_REOPEN
 * make Xlib treat a DISPLAY beginning with '/' as a launchd-managed
 * listening socket (XQuartz's /private/tmp/com.apple.launchd.XXXX/org.x:0),
 * and no X server in this port is started that way (X11GS 4 decides how
 * Xvfb/Xorg are launched; revisit this define there). */
/* CA:193-206 --enable-xthreads defaults to yes; getpwuid_r is exported
 * (libinfo.dylib) -> XUSE_MTSAFE_API. Mirrored in X11/XlibConf.h. */
#define XTHREADS 1
#define XUSE_MTSAFE_API 1
/* CA:243 */
#define USE_POLL 1                       /* poll(): syscall #230, libsystem_kernel.dylib */
/* CA:269-276 / 278-286 / 310-320: XCMS, XLOCALE and XKB all default yes. */
#define XCMS 1
#define XLOCALE 1
#define XKB 1
/* CA:301-308: xf86bigfontproto >= 1.2.0 is in xorgproto-2025.1. */
#define XF86BIGFONT 1
/* CA:325-334 COMPOSECACHE defaults to AC_FUNC_MMAP's result, a run-time
 * test that autoconf answers "no" when cross-compiling (as this build
 * and PureDarwin's nix cross-build both are). Left undefined. */

/* CA:340-393 AX_DEFINE_DIR paths, for --prefix=/usr/X11 (x11_common.sh). */
#define X11_DATADIR "/usr/X11/share/X11"
#define X11_LIBDIR "/usr/X11/lib/X11"
#define XLOCALEDATADIR "/usr/X11/share/X11/locale"
#define XLOCALELIBDIR "/usr/X11/lib/X11/locale"
#define XLOCALEDIR "/usr/X11/share/X11/locale"
#define XKEYSYMDB "/usr/X11/share/X11/XKeysymDB"
#define XERRORDB "/usr/X11/share/X11/XErrorDB"

/* CA:395 XORG_CHECK_MALLOC_ZERO: a run-time test; Apple libmalloc's
 * malloc(0) returns a unique non-NULL pointer (libmalloc-317.40.8
 * src/malloc.c: malloc() -> malloc_zone_malloc(), no zero special case),
 * so MALLOC_0_RETURNS_NULL / XMALLOC_ZERO_CFLAGS stay unset -- the answer
 * a native macOS configure gets. */
