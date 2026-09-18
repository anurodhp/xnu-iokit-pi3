/* Real, honest, documented architectural-gap stub for build_libc_db.sh
 * (DAR-174). Real Apple third_party/Libc/db/db/FreeBSD/db.c's own
 * dbopen() dispatch unconditionally references __bt_open/__rec_open in
 * its DB_BTREE/DB_RECNO switch cases -- even though nothing this
 * project currently BUILDS calls dbopen(..., DB_BTREE/DB_RECNO, ...)
 * (confirmed via grep, see build_libc_db.sh's own header; real Apple
 * system_cmds/sa.tproj/{db,pdb}.c DO call dbopen(..., DB_BTREE, ...),
 * but no build_*.sh script builds sa.tproj today, confirmed via grep --
 * so this real gap sits dormant, not exercised, until/unless that
 * changes), the real db.c object file still has an undefined reference
 * to both symbols and won't link without SOMETHING defining them.
 *
 * This project has never ported third_party/Libc/db/{btree,recno}
 * (build_libc_db.sh deliberately scopes to db/+hash/ only -- pwd_mkdb(8)
 * only ever opens DB_HASH). Rather than pull in ~15 more real,
 * unmodified-but-unexercised Apple files just to satisfy the linker,
 * these two functions are real, honestly-failing stand-ins: they do
 * exactly what dbopen()'s own real caller-facing contract says happens
 * on an unsupported/invalid request (real db.h: dbopen() returns NULL
 * with errno set on failure -- this is not new invented behavior, it's
 * the same real error path a corrupt openinfo argument would hit) and
 * nothing else. If DB_BTREE/DB_RECNO support is ever needed here, THIS
 * file should be deleted and third_party/Libc/db/{btree,recno}/FreeBSD
 * ported for real, following build_libc_db.sh's own template.
 */
#include <db.h>
#include <errno.h>
#include <stddef.h>

DB *
__bt_open(const char *fname, int flags, int mode, const BTREEINFO *openinfo, int dflags) {
    (void)fname; (void)flags; (void)mode; (void)openinfo; (void)dflags;
    errno = EINVAL; /* real DB_BTREE support not ported (see this file's own header) */
    return (NULL);
}

DB *
__rec_open(const char *fname, int flags, int mode, const RECNOINFO *openinfo, int dflags) {
    (void)fname; (void)flags; (void)mode; (void)openinfo; (void)dflags;
    errno = EINVAL; /* real DB_RECNO support not ported (see this file's own header) */
    return (NULL);
}
