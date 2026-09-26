/* libxcb-1.17.0 config.h (DAR-383). configure.ac line numbers cited. */
#include "../common_autoconf.h"
#define PACKAGE_STRING "libxcb 1.17.0"
#define PACKAGE_VERSION "1.17.0"

/* configure.ac:60-70: libXdmcp found with XdmcpWrap (built with
 * HASXDMAUTH, see x11_config/libXdmcp/config.h), so XDM-AUTHORIZATION-1
 * support is compiled into xcb_auth.c. */
#define HASXDMAUTH 1

/* configure.ac:98-138: sendmsg() is in libc and struct msghdr has
 * msg_control without any _XOPEN_SOURCE (SDK sys/socket.h), so
 * HAVE_SENDMSG and no _XOPEN_SOURCE define. sendmsg is a real export of
 * libsystem_kernel.dylib (on top of the raw ___sendmsg_nocancel trap). */
#define HAVE_SENDMSG 1

/* configure.ac:139-150: HAVE_ABSTRACT_SOCKETS is linux-only. Undefined. */

/* configure.ac:155-162: --with-queue-size default. */
#define XCB_QUEUE_BUFFER_SIZE 16384

/* configure.ac:165-170: Darwin's struct sockaddr_un has sun_len (SDK
 * sys/un.h). */
#define HAVE_SOCKADDR_SUN_LEN 1

/* configure.ac:173-174: tsol/label.h / is_system_labeled are Solaris
 * Trusted Extensions. Undefined. */

/* configure.ac:177-181: IOV_MAX is declared by the SDK's <limits.h>
 * (1024), so configure defines nothing here. */

/* configure.ac:186 XCB_CHECK_VISIBILITY (m4/xcb.m4): clang accepts
 * #pragma GCC visibility, so xcbint.h hides the library-internal
 * functions exactly as on a real build. */
#define GCC_HAS_VISIBILITY /**/

/* configure.ac:188 */
#define HAVE_GETADDRINFO 1

/* configure.ac:190-212: darwin11+ (not darwin7-10, deployment target not
 * 10.0-10.6) takes AC_CHECK_FUNC(poll) -> USE_POLL. poll() is syscall
 * #230 in xnu-7195/bsd/kern/syscalls.master, exported by
 * libsystem_kernel.dylib. */
#define USE_POLL 1
