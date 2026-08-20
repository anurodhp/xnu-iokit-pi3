/* du.c's real Libc/gen/FreeBSD/fnmatch.c needs collate.h's __LIBC__
 * branch (real __collate_range_cmp/__collate_collating_symbol/
 * __collate_equiv_class/__collate_equiv_match prototypes with the
 * correct real signatures -- their definitions already exist,
 * compiled into libc_locale.a by build_libc_locale.sh, which DOES
 * define __LIBC__; confirmed via nm, not guessed). build_simple_cmds.sh
 * deliberately does not define __LIBC__ tier-wide (it's force-included
 * into every command's compile via CFLAGS, and toggling a real Apple
 * "building Libc itself" flag globally risked unrelated macro-branch
 * changes across every one of this tier's already-verified commands,
 * not just fnmatch.c) -- this narrow wrapper (the classic
 * #define-then-#include-the-real-.c trick, same real source, zero
 * modification) scopes __LIBC__ to just this one translation unit. */
#define __LIBC__ 1
#include "../../third_party/Libc/gen/FreeBSD/fnmatch.c"
