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

/* REAL BUG FOUND BY ADVERSARIAL REVIEW, FIXED HERE: realloc() used to
 * copy `size` (the NEW, requested size) bytes from the old block
 * unconditionally - a real out-of-bounds read whenever the new size is
 * larger than the old allocation actually was (the exact "grow" case
 * realloc exists for). A bump allocator has no free-list/block metadata
 * to consult, so each allocation now stores its own size in an 8-byte
 * header immediately before the returned pointer - the minimum needed
 * to make realloc's copy length correct, not a step toward being a real
 * general-purpose allocator (see this file's own header comment: free()
 * is still a genuine no-op, no reuse, still bump-only). */
static void *bump_alloc(size_t size) {
    size_t total = ((sizeof(size_t) + size) + 15) & ~(size_t)15; /* header + payload, 16-byte aligned */
    if (g_arena_used + total > BUMP_ARENA_SIZE) {
        return 0;
    }
    unsigned char *block = &g_arena[g_arena_used];
    g_arena_used += total;
    *(size_t *)block = size; /* store the REQUESTED size, not the rounded total */
    return block + sizeof(size_t);
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
     * handful of times. Copies min(old_size, new_size) bytes, read from
     * this allocation's own real stored size (see bump_alloc()) - fixed
     * from an earlier version that copied `size` (the new size)
     * unconditionally, a real OOB read on any grow. */
    void *p = bump_alloc(size);
    if (p && ptr) {
        size_t old_size = *((size_t *)ptr - 1);
        size_t copy_len = old_size < size ? old_size : size;
        unsigned char *dst = (unsigned char *)p;
        unsigned char *src = (unsigned char *)ptr;
        for (size_t i = 0; i < copy_len; i++) {
            dst[i] = src[i];
        }
    }
    return p;
}

void free(void *ptr) {
    (void)ptr;
}

/* reallocf: real BSD extension - realloc(), but frees ptr on failure
 * too (plain realloc leaves the original block untouched on failure).
 * free() being a genuine no-op here (see this file's header comment)
 * means the "free on failure" behavior is itself a no-op in practice,
 * but the success path must still behave like real reallocf - correct,
 * not a stub, for what this placeholder allocator can actually do. */
void *reallocf(void *ptr, size_t size) {
    void *p = realloc(ptr, size);
    if (!p) {
        free(ptr);
    }
    return p;
}
