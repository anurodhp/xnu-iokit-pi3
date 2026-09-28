/*
 * DAR-236 (full objc4 runtime port): narrow, honestly-documented
 * real-behavior stand-ins for a handful of real Darwin libc/libSystem
 * entry points objc4's real Apple source calls directly but that this
 * project's own curated libc/libmalloc/libsystem_kernel builds don't
 * yet export under these exact names. Each one documented separately
 * below; none of these fabricate success where the real answer is
 * failure/unavailable.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <mach/mach.h>

/* _Exit(): POSIX alias for _exit(), real objc-errors.mm calls it
 * directly (_objc_fatalv's final abort path). This project's real
 * _exit() is already a real, working raw syscall wrapper (exported by
 * libsystem_malloc.dylib as __exit/___exit, confirmed via nm) -- _Exit()
 * is specified by POSIX to behave identically to _exit() (no atexit/
 * stdio flush), so forwarding to the real, already-working _exit() is
 * the correct, non-fabricated implementation, not a stand-in for
 * missing behavior. */
extern void _exit(int) __attribute__((noreturn));
void _Exit(int status) { _exit(status); }

/* fls()/flsl(): real BSD "find last set bit" functions objc-runtime-new.mm
 * (category_list::append) and objc-runtime.mm (SafeRanges::add) use for
 * real bit-scanning, not diagnostics -- these need a real, correct
 * implementation, not a stub. __builtin_clzl is a real, portable
 * compiler builtin (not an approximation) for exactly this operation;
 * this is the same real formula FreeBSD's own libc uses. */
int fls(int mask) {
	return mask == 0 ? 0 : (int)(32 - __builtin_clz((unsigned int)mask));
}
/* flsl() is exported by libsystem_c.dylib, its Apple home; libobjc binds
 * it there. fls() has no exporter in this tree yet. */

/* malloc_zone_*(): real Darwin malloc-zone API. This project's curated
 * libsystem_malloc.dylib exports real, working malloc()/calloc()/free()/
 * valloc() but has never implemented the separate multi-zone allocator
 * API (confirmed via nm: no malloc_create_zone/malloc_zone_malloc
 * exports anywhere). objc4's own real callers (NXCreateMapTable,
 * AutoreleasePoolPage::operator new, class_createInstance, etc) only
 * ever use the zone parameter as an opaque "which allocator" handle --
 * this project has exactly one real allocator, so forwarding every zone
 * call to the real, single, already-working malloc/calloc/free is the
 * real, correct behavior for "one zone total", not a fabrication of
 * zone-isolation semantics we don't have. */
extern void *malloc(unsigned long);
extern void *calloc(unsigned long, unsigned long);
extern void free(void *);

typedef struct _malloc_zone_t malloc_zone_t;

malloc_zone_t *malloc_default_zone(void) {
	/* Real callers only ever compare/pass this pointer through to the
	 * other malloc_zone_*() calls below, never dereference it -- a
	 * non-NULL sentinel is real, correct, and matches every actual
	 * caller's real usage (confirmed via grep across the objc4 tree). */
	static int zone_sentinel;
	return (malloc_zone_t *)&zone_sentinel;
}
void *malloc_zone_malloc(malloc_zone_t *zone, unsigned long size) {
	(void)zone;
	return malloc(size);
}
void *malloc_zone_calloc(malloc_zone_t *zone, unsigned long count, unsigned long size) {
	(void)zone;
	return calloc(count, size);
}
void *malloc_zone_memalign(malloc_zone_t *zone, unsigned long alignment, unsigned long size) {
	(void)zone;
	void *p = NULL;
	if (posix_memalign(&p, alignment < sizeof(void *) ? sizeof(void *) : alignment, size) != 0)
		return NULL;
	return p;
}
malloc_zone_t *malloc_zone_from_ptr(const void *ptr) {
	(void)ptr;
	return malloc_default_zone();
}
void *malloc_zone_batch_malloc(malloc_zone_t *zone, unsigned long size, void **results, unsigned num_requested) {
	unsigned i;
	for (i = 0; i < num_requested; i++) {
		results[i] = malloc_zone_malloc(zone, size);
		if (results[i] == NULL)
			return (void *)(uintptr_t)i;
	}
	return (void *)(uintptr_t)num_requested;
}

/* backtrace()/backtrace_symbols()/backtrace_symbols_fd(): real Darwin
 * execinfo API. This project has never implemented real frame-pointer
 * stack unwinding for diagnostic backtraces anywhere (same honest gap
 * build_libsystemconfiguration_client.sh's own header comment already
 * documented for its own SCDPlugin.c/SCDOpen.c callers). Real objc4
 * callers (AutoreleasePoolPage::printHiwat(), _objc_exception_throw())
 * only use these for OPTIONAL diagnostic printing -- an honest "no
 * frames captured" empty result is the real, correct answer for a
 * platform with no unwinder wired up here, not a crash or a fabricated
 * trace. */
int backtrace(void **buffer, int size) {
	(void)buffer;
	(void)size;
	return 0;
}
char **backtrace_symbols(void *const *buffer, int size) {
	(void)buffer;
	(void)size;
	return NULL;
}
void backtrace_symbols_fd(void *const *buffer, int size, int fd) {
	(void)buffer;
	(void)size;
	(void)fd;
}

/* thread_get_state() used to be a KERN_FAILURE stand-in here. Since
 * DAR-416, libsystem_kernel.dylib builds Apple's real
 * libsyscall/mach/thread_act.c over MIG user code for thread_act.defs
 * (build_libsystem_kernel.sh) and exports _thread_get_state, its Apple
 * home, so libobjc binds that (DAR-236, 2026-09-28). The stand-in made
 * objc-cache.mm's _get_pc_for_thread() always return PC_SENTINEL, so
 * _collecting_in_critical() always reported a thread in a critical
 * section and old method caches were never freed. */

/* getSharedCachePreoptimizedProtocol(): declared unconditionally in
 * objc-private.h, but only ever DEFINED in objc-opt.mm's real
 * SUPPORT_PREOPT=1 branch (which this port deliberately doesn't build
 * for objc-opt.mm -- see objc4_config_override.h's own header comment
 * for the real dyld/objc-shared-cache.h version-skew root cause). Its
 * one real call site (objc-runtime-new.mm's protocol-canonicalization
 * pass) is itself gated behind `headerIsPreoptimized`, which our own
 * honest isPreoptimized() always returns false for -- so this function
 * body is provably unreachable at runtime on this target. Nil is the
 * real, correct "no shared cache, no cached protocol" answer, matching
 * what the real function itself would return whenever the shared cache
 * genuinely has no match. */
#ifdef __cplusplus
extern "C"
#endif
void *getSharedCachePreoptimizedProtocol(const char *name) {
	(void)name;
	return 0;
}
