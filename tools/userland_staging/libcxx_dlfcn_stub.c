/* Real dlopen()/dlsym(), scoped to exactly what real LLVM libc++ needs
 * them for on this target: libcxx/src/include/refstring.h's
 * compute_gcc_empty_string_storage() (built into stdexcept.cpp) probes
 * for a co-loaded libstdc++.6.dylib to interoperate with its COW empty-
 * string representation, purely as an optional runtime capability check
 * (a null return from dlopen()/dlsym() is real libc++'s own documented
 * "not available" path, not an error it panics on -- see that header's
 * own null checks). NULL/nullptr is the real, correct answer here since
 * this project never ships a libstdc++.6.dylib to find, regardless of
 * dyld.
 *
 * UPDATED 2026-08-31 (static-linking cleanup, dylib-vs-dylib duplicate-
 * symbol pass): the header comment used to justify this stub with "this
 * target has no dyld at all" -- stale since Phase 6.5 (real dyld exists,
 * and this project's real libdyld.dylib genuinely implements dlopen()/
 * dlsym() for real, per libdyld_compat_misc.c-adjacent LibSystemHelpers
 * wiring). The real, CURRENT reason this stays a separate local copy
 * instead of linking libdyld.dylib's real ones: confirmed via `otool -L`
 * that libdyld.dylib already links libc++.dylib directly (needs real
 * C++ runtime support), so libc++.dylib linking libdyld.dylib back would
 * be a real mutual-dependency cycle between the two dylibs -- the same
 * "genuinely cannot link its real owner back due to dependency-order/
 * circularity" shape already established for libunwind.dylib's own
 * dyld-unwind trio (DAR-18) and libsystem_malloc.dylib's memcpy/memset
 * (DAR-13). Confirmed via `dup_symbol_audit.sh --explain _dlopen` still
 * showing exactly libc++.dylib/libdyld.dylib as the two owners with the
 * cycle intact; not touched further this pass. Bundled directly into
 * libcxx.a (build_llvm_libcxx.sh) so every consumer gets it, the same
 * shape as build_libiconv.sh bundling qsort.c/heapsort.c or
 * build_libpthread.sh bundling simple_cmds_mmap_compat.c directly into
 * its own archive.
 */

void *dlopen(const char *path, int mode) {
    (void)path;
    (void)mode;
    return (void *)0;
}

void *dlsym(void *handle, const char *symbol) {
    (void)handle;
    (void)symbol;
    return (void *)0;
}
