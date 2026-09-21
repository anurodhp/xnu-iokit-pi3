/* pw(8) build-wide augmentation of <sys/wait.h> (DAR-341).
 *
 * `_wait4()` -- real, unmodified Apple Libc/gen/FreeBSD/popen.c (pulled
 * in for real popen()/pclose(), see build_pw.sh's own header) calls the
 * underscore-prefixed internal `_wait4(...)` directly (real FreeBSD/BSD
 * libc convention: an internal, `namespace.h`-protected alternate name
 * for a public libc function, used inside libc's own source so an
 * application's own interposed `wait4()` can't accidentally redirect
 * libc's internal call). This project's own minimal
 * third_party/Libc/fbsdcompat/namespace.h stub (used project-wide, see
 * that file directly) does not define this particular alias, and this
 * project's real exported symbol is plain `wait4` (confirmed via
 * `nm -gU` against libc_build/system/libsystem_c.dylib -- no `_wait4`
 * export under either name). Declared here so the call compiles; a
 * `-Wl,-alias` in build_pw.sh's own final link step bridges the real
 * exported `_wait4`(Darwin-mangled `wait4`) symbol to the wanted
 * `__wait4`(Darwin-mangled `_wait4`) one -- same real
 * declare-here-alias-at-link-time shape already used throughout this
 * script for the `$UNIX2003`-suffixed cases.
 */
#ifndef _PW_SYS_WAIT_AUGMENT_H_
#define _PW_SYS_WAIT_AUGMENT_H_

#include_next <sys/wait.h>

#include <sys/types.h>
struct rusage;
pid_t _wait4(pid_t, int *, int, struct rusage *);

#endif
