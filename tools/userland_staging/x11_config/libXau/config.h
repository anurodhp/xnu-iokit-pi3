/* libXau-1.0.12 config.h (DAR-383). configure.ac line numbers cited. */
#include "../common_autoconf.h"
#define PACKAGE_VERSION "1.0.12"

/* configure.ac:48 AC_CHECK_FUNCS([explicit_bzero explicit_memset pathconf]).
 * explicit_bzero/explicit_memset: absent from the iOS 14.4 SDK headers
 * (Darwin has memset_s instead), so both stay undefined and AuDispose.c /
 * AuRead.c fall back to their own volatile-pointer bzero loop, exactly as
 * on a real macOS configure run.
 * pathconf: real, declared in <unistd.h>; syscall #191 in
 * xnu-7195/bsd/kern/syscalls.master, exported by libsystem_kernel.dylib. */
#define HAVE_PATHCONF 1

/* configure.ac:56-62: --enable-xthreads defaults to yes. */
#define XTHREADS 1
/* configure.ac:63-70: XUSE_MTSAFE_API only if libc has gethostbyname_r,
 * which Darwin never has (not in the SDK's netdb.h). Left undefined. */
