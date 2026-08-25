/* Real dlopen()/dlsym(), scoped to exactly what real LLVM libc++ needs
 * them for on this target: libcxx/src/include/refstring.h's
 * compute_gcc_empty_string_storage() (built into stdexcept.cpp) probes
 * for a co-loaded libstdc++.6.dylib to interoperate with its COW empty-
 * string representation, purely as an optional runtime capability check
 * (a null return from dlopen()/dlsym() is real libc++'s own documented
 * "not available" path, not an error it panics on -- see that header's
 * own null checks). This target has no dyld at all, so nothing can ever
 * be dynamically loaded -- NULL/nullptr is the real, correct answer to
 * "is libstdc++ already loaded", not a fabricated stand-in for a missing
 * feature. Bundled directly into libcxx.a (build_llvm_libcxx.sh) so
 * every consumer gets it, the same shape as build_libiconv.sh bundling
 * qsort.c/heapsort.c or build_libpthread.sh bundling
 * simple_cmds_mmap_compat.c directly into its own archive.
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
