/* libICE-1.1.2 config.h (DAR-383). configure.ac (CA) lines cited. */
#include "../common_autoconf.h"
#define PACKAGE_VERSION "1.1.2"
/* CA:51 AC_CHECK_FUNCS([asprintf arc4random_buf getentropy]) -- all three
 * exported by libsystem_c.dylib. */
#define HAVE_ASPRINTF 1
#define HAVE_ARC4RANDOM_BUF 1
#define HAVE_GETENTROPY 1
/* CA:54 */
#include "../xtrans_flags.h"
/* CA:55-57 */
#define ICE_t 1
#define TRANS_CLIENT 1
#define TRANS_SERVER 1
