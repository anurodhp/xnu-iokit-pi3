/* libSM-1.2.6 config.h (DAR-383). configure.ac (CA) lines cited. */
#include "../common_autoconf.h"
#define PACKAGE_VERSION "1.2.6"
/* CA:31 AC_CHECK_FUNCS([uuid_create]): that is the BSD/DCE libc API, not
 * Darwin's (Darwin has libuuid-style uuid_generate in libSystem, and
 * there is no uuid.pc here for CA:33's PKG_CHECK_MODULES either), so
 * neither HAVE_UUID_CREATE nor HAVE_LIBUUID: CA:53-59 falls back to
 * xtrans-based network-address client ids. */
#include "../xtrans_flags.h"          /* CA:58 XTRANS_CONNECTION_FLAGS */
/* CA:59 AC_CHECK_FUNCS([getaddrinfo]) -- already HAVE_GETADDRINFO above. */
