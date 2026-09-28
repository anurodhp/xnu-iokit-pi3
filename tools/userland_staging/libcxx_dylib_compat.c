/* Real gaps found migrating libcxx.a+libcxxabi.a into one real
 * libc++.dylib (docs/daemon-roadmap.md's "Static-vs-dylib architecture
 * debt" section, migration step 5).
 *
 * catopen/catgets/catclose: real source exists
 * (third_party/Libc/nls/FreeBSD/msgcat.c) but NLS message-catalog
 * support has never been built anywhere in this project -- real
 * libc++'s <locale> messages facet (locale.cpp) references these
 * unconditionally even though nothing in this project's own code uses
 * std::messages. Real, standard POSIX behavior for "no catalogs
 * available" (not a lie -- this target genuinely has none installed):
 * catopen() returns (nl_catd)-1, catgets() returns the caller-supplied
 * default string unchanged, catclose() returns -1. Every real libc
 * implementation returns exactly this when NLCATPATH is unset/empty,
 * which is this target's genuine, permanent state.
 *
 * _dyld_find_unwind_sections/_dyld_register_func_for_remove_image/
 * dladdr: same real gap and same real fix build_libsystem.sh's own
 * dyld_unwind_shim_compat.c already established for libSystem.B.dylib
 * (see that script's own comment) -- libunwind's real
 * libunwind_dyld_shim.o needs __mh_execute_header (never synthesized
 * for a dylib), and libdyld.dylib's own real libdyldGlue.o (which DOES
 * provide real implementations) isn't a dependency here -- linking
 * against it would be circular (libdyld.dylib itself links libc++.dylib
 * for RTTI). Copied verbatim rather than re-derived twice.
 *
 * HIDDEN 2026-09-03 (DAR-6/DAR-18): the circularity above means
 * _dyld_find_unwind_sections/_dyld_register_func_for_remove_image/
 * dladdr can never link their real owner (libdyld.dylib) back, so the
 * duplicate symbol itself can't go away here either -- but same as
 * libcxx_dlfcn_stub.c's own dlopen()/dlsym() (DAR-6), these three never
 * needed to be EXPORTED, only callable by this dylib's own real
 * internal caller (confirmed via `nm -u` on the extracted libunwind.a
 * member set: libunwind.o itself is the one real caller of all three,
 * for its phase-1 DWARF-CFI search over dynamically-loaded images).
 * Confirmed no shipped image currently binds to `libunwind/_dladdr` (or
 * the other two) specifically (`xcrun dyld_info -fixups` swept across
 * every shipped dylib/macho) -- unlike libc++.dylib's dlopen/dlsym,
 * this one wasn't yet observed shadowing a real caller, but the same
 * risk exists structurally (a future consumer linking libunwind.dylib
 * before libdyld.dylib would silently get this honest-but-fake
 * always-"nothing found" stub instead of the real one). Marking these
 * `hidden` removes them from this dylib's export trie (confirmed via
 * `dyld_info -exports`), closing the risk before it bites the way
 * libc++.dylib's copy already had. catopen/catgets/catclose/
 * clock_gettime below are NOT touched -- they aren't duplicates of any
 * other shipped dylib (0 owners besides this one in
 * `dup_symbol_audit.sh --summary`), so there's no real "wrong owner"
 * risk to close for them, and hiding them isn't in scope for this pass.
 */
typedef void *nl_catd;
nl_catd catopen(const char *name, int oflag) {
    (void)name; (void)oflag;
    return (nl_catd)-1;
}
char *catgets(nl_catd catd, int set_id, int msg_id, const char *s) {
    (void)catd; (void)set_id; (void)msg_id;
    return (char *)s;
}
int catclose(nl_catd catd) {
    (void)catd;
    return -1;
}

/* clock_gettime used to be a hand-written stand-in here. Since DAR-416 1b it
 * is Libc's real gen/clock_gettime.c in libsystem_c.dylib, its Apple home,
 * and libc++ binds it there. */

__attribute__((visibility("hidden")))
int _dyld_find_unwind_sections(void *addr, void *info) {
    (void)addr; (void)info;
    return 0;
}
__attribute__((visibility("hidden")))
void _dyld_register_func_for_remove_image(void (*func)(const void *, unsigned long)) {
    (void)func;
}
__attribute__((visibility("hidden")))
void *dladdr(const void *addr, void *info) {
    (void)addr; (void)info;
    return 0;
}
