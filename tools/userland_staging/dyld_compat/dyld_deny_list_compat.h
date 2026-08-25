/* Real, confirmed gap: `inDenyList()` (called once, dyld2.cpp, gates a
 * hardcoded per-app dyld2/dyld3 compatibility override) is not declared
 * or defined anywhere in this apple-oss-distributions dyld-832.7.3
 * snapshot -- a real redaction/omission, not a header search-path miss
 * (grepped the full checkout, see build_dyld_core.sh's header comment).
 * A from-scratch port has no legacy binaries needing compatibility
 * exceptions, so "no app is ever on the deny list" (always false) is
 * the real, correct behavior here, not a stand-in for missing logic.
 * C++-only: dyld2.cpp is the only real consumer; the plain-C dyld
 * support files (dyldExceptions.c etc) that also -include this header
 * don't reference it at all and don't have `bool` without <stdbool.h>.
 */
#ifndef _COMPAT_DYLD_DENY_LIST_H_
#define _COMPAT_DYLD_DENY_LIST_H_
#ifdef __cplusplus
static inline bool inDenyList(const char *execPath) {
    (void)execPath;
    return false;
}
#endif
#endif
