/* flsl (find-last-set, 1-indexed highest set bit, 0 for input 0): real
 * third_party/Libc/stdlib/FreeBSD/qsort.c (built into libiconv.a by
 * build_libiconv.sh -- Apple's real iconvlist() calls qsort() to sort
 * alias names for display) uses it for its introsort-style recursion-
 * depth heuristic (DEPTH(x) macro), but no real .c implementation of
 * flsl exists anywhere in this project's Libc checkout -- only
 * strings.h declares it (confirmed by grep, same finding as the
 * identical real gap tools/userland_staging/removefile_syscalls.c hit
 * first while porting removefile's own qsort.c call site).
 *
 * This is the SAME real, well-defined, last-resort implementation as
 * that file's flsl() (removefile_syscalls.c:164) -- using
 * __builtin_clzl, a real compiler builtin lowering to a real arm64 CLZ
 * instruction, not approximated -- duplicated here as a tiny standalone
 * file instead of linking the unrelated libremovefile.a into libiconv.a
 * just for this one symbol (that would pull in a whole other
 * subsystem's object list for no reason -- a real, avoidable, odd
 * cross-library dependency, not a necessary one).
 */
int flsl(long mask) {
    if (mask == 0) return 0;
    unsigned long umask = (unsigned long)mask;
    return (int)(sizeof(long) * 8) - __builtin_clzl(umask);
}
