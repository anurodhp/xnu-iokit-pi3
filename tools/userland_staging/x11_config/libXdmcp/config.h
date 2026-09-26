/* libXdmcp-1.1.5 config.h (DAR-383). configure.ac line numbers cited. */
#include "../common_autoconf.h"
#define PACKAGE_VERSION "1.1.5"

/* configure.ac:57 AC_CHECK_HEADERS([sys/random.h]): absent from the
 * iOS 14.4 SDK (Darwin declares getentropy() in <sys/random.h> only on
 * macOS SDKs). Left undefined. */

/* configure.ac:82 AC_CHECK_FUNCS([srand48 lrand48 arc4random_buf getentropy]).
 * arc4random_buf and getentropy are real exports of libsystem_c.dylib.
 * With HAVE_ARC4RANDOM_BUF defined, Key.c:36-91 compiles none of its
 * lrand48/getentropy fallback, so srand48/lrand48 (not exported by any
 * dylib in this tree today) are deliberately left undefined rather than
 * claimed. */
#define HAVE_ARC4RANDOM_BUF 1
#define HAVE_GETENTROPY 1

/* configure.ac:87-90: Wraphelp.c ships in the tag, so HASXDMAUTH. */
#define HASXDMAUTH 1
