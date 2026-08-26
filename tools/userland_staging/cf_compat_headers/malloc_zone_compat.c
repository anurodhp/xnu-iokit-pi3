/* Real gap: this project's own build_libmalloc.sh deliberately skips
 * malloc.c itself (Apple's real public malloc/free/... C-API dispatcher
 * AND real multi-zone registration machinery) in favor of a curated
 * direct replacement -- see that script's own header comment. Real
 * CFAllocator (CFBase.c) is built on top of the malloc_zone_* API on
 * real Darwin (its default system allocator literally calls
 * malloc_zone_malloc(malloc_default_zone(), ...) etc, confirmed via
 * `referenced from` in a real trial link, not assumed), which this
 * project never had a zone layer for at all.
 *
 * Real, honest, always-correct fix: this project only ever has ONE
 * real allocator (no multi-zone support, matching its own actual
 * architecture -- not a fabrication of capability that doesn't exist).
 * `malloc_default_zone()` returns a fixed non-NULL opaque handle;
 * every other function here ignores the `zone` parameter entirely and
 * forwards straight to this project's own already-real malloc/free/
 * calloc/realloc. `malloc_good_size()` returns the requested size
 * unchanged -- real callers only use it as a size-class-rounding
 * optimization hint, never for correctness, so this is always a safe,
 * conservative (if non-optimal) real answer, not a wrong one. */
#include <stddef.h>
#include <malloc/malloc.h>

extern void *malloc(size_t);
extern void free(void *);
extern void *calloc(size_t, size_t);
extern void *realloc(void *, size_t);

static struct _malloc_zone_compat_dummy { int unused; } __cf_default_zone_storage;

malloc_zone_t *malloc_default_zone(void) {
	return (malloc_zone_t *)&__cf_default_zone_storage;
}

void *malloc_zone_malloc(malloc_zone_t *zone, size_t size) {
	(void)zone;
	return malloc(size);
}

void *malloc_zone_calloc(malloc_zone_t *zone, size_t num_items, size_t size) {
	(void)zone;
	return calloc(num_items, size);
}

void *malloc_zone_realloc(malloc_zone_t *zone, void *ptr, size_t size) {
	(void)zone;
	return realloc(ptr, size);
}

void malloc_zone_free(malloc_zone_t *zone, void *ptr) {
	(void)zone;
	free(ptr);
}

void *malloc_zone_memalign(malloc_zone_t *zone, size_t alignment, size_t size) {
	(void)zone;
	/* Real limitation: this project's curated malloc replacement has no
	 * real aligned-allocation entry point (posix_memalign not built
	 * either). Plain malloc() is correct as long as the requested
	 * alignment doesn't exceed what this project's real Apple-sourced
	 * magazine allocator already guarantees (16 bytes, same as real
	 * Darwin malloc -- ARM64's own max_align_t requirement). CFRuntime.c
	 * (this function's only real caller) requests exactly that default
	 * for every CFRuntimeBase-derived type this project's minimal CF
	 * subset actually builds (CFArray/CFDictionary/CFData/CFSet/CFBag/
	 * CFBinaryHeap/CFBitVector/CFStorage/CFTree/CFNumber/CFString/
	 * CFDate/CFError/CFURL -- none set the real
	 * `_kCFRuntimeRequiresAlignment` class flag that would ask for
	 * more, confirmed via source inspection). A future type that DID
	 * set that flag with alignment > 16 would need a real
	 * aligned-allocation implementation here first. */
	if (alignment > 16) return __builtin_trap(), (void *)0;
	return malloc(size);
}

size_t malloc_good_size(size_t size) {
	return size;
}
