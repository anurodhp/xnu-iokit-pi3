/* libXfont2-2.0.7 config.h (DAR-387). configure.ac (CA) lines cited. */
#include "../common_autoconf.h"
#define PACKAGE_VERSION "2.0.7"
/* CA:52 AC_CHECK_HEADERS([endian.h poll.h sys/poll.h]): the iPhoneOS 14.4
 * SDK has poll.h and sys/poll.h, not endian.h. */
#define HAVE_POLL_H 1
#define HAVE_SYS_POLL_H 1
/* CA:66 AC_CHECK_FUNCS([poll readlink strlcat]): all three are exported by
 * this tree's libsystem_kernel/libsystem_c (readlink since DAR-387). */
#define HAVE_POLL 1
#define HAVE_READLINK 1
#define HAVE_STRLCAT 1
/* CA:68 AC_REPLACE_FUNCS([reallocarray realpath strlcat strlcpy]).
 * strlcat/strlcpy link, so no replacement. reallocarray is not exported here
 * (see x11_config/libXext/config.h), so build_libXfont2.sh compiles
 * src/util/reallocarray.c, as configure would. realpath is not exported by
 * any libsystem_* dylib either, but src/util/realpath.c only has a WIN32
 * body (realpath.c:23-29), and nothing in libXfont2 calls realpath()
 * (its only mention outside that file is src/util/replace.h:68's
 * declaration), so it is not compiled. */
#define HAVE_STRLCPY 1
/* CA:71 AC_CHECK_HEADERS([err.h float.h]) */
#define HAVE_ERR_H 1
#define HAVE_FLOAT_H 1
/* CA:86-92 --enable-freetype defaults to yes */
#define XFONT_FREETYPE 1
/* CA:135 unconditional */
#define X_GZIP_FONT_COMPRESSION 1
/* CA:147-153 --with-bzip2 defaults to no: X_BZIP2_FONT_COMPRESSION unset. */
/* CA:157-164 --enable-builtins defaults to yes (sets XFONT_BITMAP) */
#define XFONT_BUILTINS 1
/* CA:167-174 --enable-pcfformat defaults to yes */
#define XFONT_PCFFORMAT 1
/* CA:180-187 --enable-bdfformat defaults to yes */
#define XFONT_BDFFORMAT 1
/* CA:190-197 --enable-snfformat defaults to no: XFONT_SNFFORMAT unset. */
/* CA:200-203 */
#define XFONT_BITMAP 1
/* CA:206-209 (freetype and bitmap both set XFONT_FONTFILE) */
#define XFONT_FONTFILE 1
/* CA:215-222 --enable-fc (xfs font-server client) defaults to yes; this build
 * passes the equivalent of --disable-fc: Xvfb here serves only local font
 * files, and the xfs client pulls in xtrans's TCP/UNIX client transport for
 * a font server nothing on this system runs. XFONT_FC unset, and
 * build_libXfont2.sh leaves out src/fc/. */
