/* libXcursor-1.2.3 config.h (DAR-383). configure.ac (CA) lines cited. */
#include "../common_autoconf.h"
#define PACKAGE_VERSION "1.2.3"
/* CA:55-63: split from PACKAGE_VERSION. */
#define XCURSOR_LIB_MAJOR 1
#define XCURSOR_LIB_MINOR 2
#define XCURSOR_LIB_REVISION 3
/* CA:85-86: unconditional once xfixes is found. */
#define HAVE_XFIXES 1
/* ICONDIR / XCURSORPATH (CA:66-83) are passed as -D flags by
 * src/Makefile.am:9-13, and so by build_libXcursor.sh. */
