/* Real gap: see dyld_compat/System/atexit.h's own comment for the full
 * story -- this project's pinned upstream libcxxabi (llvmorg-11.1.0)
 * never grew Apple's own libcxxabi fork's `__cxa_finalize_ranges`
 * dlclose-time extension. Only ever reached from dyld's own dylib-UNLOAD
 * path, which this port doesn't exercise yet -- honest no-op, not a
 * stand-in for missing logic that's actually needed today. */
struct __cxa_range_t {
    const void *addr;
    unsigned long length;
};

void __cxa_finalize_ranges(const struct __cxa_range_t ranges[], unsigned int count) {
    (void)ranges;
    (void)count;
}
