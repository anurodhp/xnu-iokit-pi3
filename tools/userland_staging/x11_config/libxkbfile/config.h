/* libxkbfile-1.1.3 config.h (DAR-387). configure.ac (CA) lines cited. */
#include "../common_autoconf.h"
#define PACKAGE_VERSION "1.1.3"
/* CA:45 AC_CHECK_FUNCS([strcasecmp strndup]): both exported by this tree's
 * libsystem_c.dylib. */
#define HAVE_STRCASECMP 1
#define HAVE_STRNDUP 1
/* CA:46-47 AC_CHECK_FUNC(getc_unlocked): exported by libsystem_c.dylib. */
#define HAVE_UNLOCKED_STDIO 1
