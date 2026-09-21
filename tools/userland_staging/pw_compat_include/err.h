/* pw(8) build-wide augmentation of <err.h> (DAR-341).
 *
 * `_err()` -- real Apple Libc/gen/FreeBSD/stringlist.c (pw_conf.c's own
 * real default-groups parsing / pw_user.c's own real group-membership
 * building need StringList's sl_init()/sl_add(), see build_pw.sh's own
 * header) calls `_err(1, "stringlist: %m")` twice. This project's own
 * pinned Apple Libc/gen/FreeBSD/err.c already DEFINES `_err()`
 * (confirmed via grep, `_err(int eval, const char *fmt, ...)`) but this
 * project's own pinned Apple Libc/include/err.h never DECLARES it (real
 * FreeBSD's own <err.h> does) -- a missing-prototype gap, not a missing-
 * implementation one.
 */
#ifndef _PW_ERR_AUGMENT_H_
#define _PW_ERR_AUGMENT_H_

#include_next <err.h>

#include <sys/cdefs.h>
void _err(int, const char *, ...) __cold __dead2 __printflike(2, 3);

#endif
