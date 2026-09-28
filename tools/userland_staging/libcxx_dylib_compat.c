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
 * dladdr used to be hidden stand-ins here as well; see the DAR-236 note
 * at the end of this file.
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

/* _dyld_find_unwind_sections/_dyld_register_func_for_remove_image/dladdr
 * used to be hidden always-"not found" stubs here. With them, libunwind
 * never found any image's __unwind_info/__eh_frame, so no C++ or
 * Objective-C exception was ever caught in any process: every throw went
 * to std::terminate (DAR-236, 2026-09-28). libunwind.dylib now binds the
 * real ones in libdyld.dylib, as Apple's does
 * (third_party/libunwind/libunwind/src/CMakeLists.txt:113, -ldyld); see
 * build_libunwind_dylib.sh and libdyld_for_libunwind.tbd. */
