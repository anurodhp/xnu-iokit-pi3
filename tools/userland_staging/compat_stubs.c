/* Small, honestly-scoped compat shims needed to link Libc's locale
 * subsystem (third_party/Libc/locale/xlocale.c and friends) - NOT a
 * general-purpose allocator or lock implementation, see each section.
 */

typedef unsigned long size_t;

/* ---- os_unfair_lock / OSAtomic*Barrier -----------------------------------
 * Real xlocale.c uses these for thread-safe locale refcounting/mutation.
 * This target is provably single-threaded (see tsd_bootstrap.c/
 * pthread_tsd_shim.c's own identical reasoning) - there is no concurrent
 * access for these to protect against, so a true no-op lock and plain
 * (non-atomic) increment/decrement preserve correctness here, they don't
 * approximate it. Real implementations if this target ever becomes
 * multi-threaded - not before.
 */

typedef struct { unsigned int _unused; } os_unfair_lock_s;
typedef os_unfair_lock_s *os_unfair_lock_t;

void os_unfair_lock_lock(os_unfair_lock_t lock) {
    (void)lock;
}

void os_unfair_lock_unlock(os_unfair_lock_t lock) {
    (void)lock;
}

int OSAtomicIncrement32Barrier(volatile int *value) {
    return ++(*value);
}

int OSAtomicDecrement32Barrier(volatile int *value) {
    return --(*value);
}

/* ---- placeholder bump allocator ------------------------------------------
 * NOT the real Tier-2 malloc/calloc/free/realloc this project's roadmap
 * (docs/roadmap.md Phase 4) already lists as its own separate, larger
 * future item - this is deliberately just enough to let a one-shot,
 * short-lived smoketest process link and run to prove the locale/TSD
 * work above is real, not a substitute for real heap management.
 * free() is a genuine no-op (not a bug: nothing here ever needs to
 * reclaim memory within a single short smoketest run) - documented, not
 * hidden. Bump-only, no reuse, will exhaust g_arena on any sustained use;
 * do not build real userland on top of this.
 */

#define BUMP_ARENA_SIZE (4 * 1024 * 1024)
__attribute__((aligned(16))) static unsigned char g_arena[BUMP_ARENA_SIZE];
static size_t g_arena_used = 0;

static void *bump_alloc(size_t size) {
    size = (size + 15) & ~(size_t)15; /* 16-byte align every allocation */
    if (g_arena_used + size > BUMP_ARENA_SIZE) {
        return 0;
    }
    void *p = &g_arena[g_arena_used];
    g_arena_used += size;
    return p;
}

void *malloc(size_t size) {
    return bump_alloc(size);
}

void *calloc(size_t count, size_t size) {
    size_t total = count * size; /* smoketest-only: no overflow-checking needed */
    void *p = bump_alloc(total);
    if (p) {
        unsigned char *b = (unsigned char *)p;
        for (size_t i = 0; i < total; i++) {
            b[i] = 0;
        }
    }
    return p;
}

void *realloc(void *ptr, size_t size) {
    /* Bump-only: can't grow in place or reclaim the old block. Correct
     * (if wasteful) for a smoketest that only ever grows small buffers a
     * handful of times. */
    void *p = bump_alloc(size);
    if (p && ptr) {
        unsigned char *dst = (unsigned char *)p;
        unsigned char *src = (unsigned char *)ptr;
        for (size_t i = 0; i < size; i++) {
            dst[i] = src[i];
        }
    }
    return p;
}

void free(void *ptr) {
    (void)ptr;
}
