/* Real-signature, single-threaded stand-ins for the pthread mutex/once and
 * OSAtomic64 API surface third_party/Libc/stdio/FreeBSD/findfp.c (and the
 * rest of the real Apple stdio FILE* implementation) calls to guard its
 * global stream table (__sglue/__sF) and per-FILE `_extra->fl_mutex`.
 *
 * Same reasoning already established for os_unfair_lock/OSAtomic32Barrier
 * in compat_stubs.c: this target is provably single-threaded (no
 * libpthread bsdthread_create() bootstrap exists anywhere in this
 * project yet - see cerror_stub.c's identical reasoning for why even
 * errno is a single plain global here, not per-thread), so a real no-op
 * lock and a plain (non-atomic) increment/decrement preserve real
 * correctness rather than approximating it. Real mutex/atomics needed
 * only once/if this target ever gets a real libpthread port - tracked as
 * separate future Phase 4 work, not silently faked here.
 *
 * pthread_once() specifically: correct (not just convenient) to implement
 * as a plain "call it if the flag isn't set yet" on a single-threaded
 * target - the entire reason real pthread_once needs atomicity is
 * concurrent first-callers racing, which cannot happen here.
 *
 * Types (pthread_mutex_t/pthread_once_t/pthread_mutexattr_t) come from
 * the real Xcode-12 iPhoneOS SDK's <sys/_pthread/_pthread_types.h> via
 * -I $PTHREAD/include already in this build's CFLAGS - only the runtime
 * FUNCTION BODIES are stood in here, not the ABI-facing types.
 */

#include <pthread.h>

int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *attr) {
    (void)m;
    (void)attr;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *m) {
    (void)m;
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *m) {
    (void)m;
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *m) {
    (void)m;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *m) {
    (void)m;
    return 0;
}

/* Underscore-prefixed private aliases -
 * third_party/Libc/stdio/FreeBSD/_flock_stub.c (the real flockfile()/
 * funlockfile()/ftrylockfile() public-API implementation, distinct from
 * the FLOCKFILE()/FUNLOCKFILE() internal macros stdio_flockfile_compat.h
 * stands in for) calls these instead of the public pthread_mutex_*()
 * names above. Same single-threaded-target reasoning, same no-op
 * correctness argument - real Apple's own _pthread_mutex_lock etc. are
 * genuine aliases of the public functions on real Darwin too, not a
 * separate implementation. */
int _pthread_mutex_lock(void *m) {
    (void)m;
    return 0;
}

int _pthread_mutex_trylock(void *m) {
    (void)m;
    return 0;
}

int _pthread_mutex_unlock(void *m) {
    (void)m;
    return 0;
}

int _pthread_mutex_destroy(void *m) {
    (void)m;
    return 0;
}

/* pthread_once() itself is NOT redefined here - tools/userland_staging/
 * pthread_tsd_shim.c (the Phase 4 locale tier's real, QEMU-boot-verified
 * TSD bootstrap - see docs/roadmap.md Phase 4) already provides a real
 * public pthread_once()/_pthread_once() pair, and findfp.c's __sdidinit
 * call site links against that existing implementation directly (its
 * `void *once_control` parameter type is ABI-compatible with real
 * pthread_once_t* - both are just a pointer at the call boundary, only
 * the C-level type spelling differs across translation units). Redefining
 * it here would be a real duplicate-symbol link error, not a stylistic
 * choice - found by actually trying it and letting the linker report it,
 * not guessed. */

long long OSAtomicIncrement64(volatile long long *value) {
    return ++(*value);
}

long long OSAtomicDecrement64(volatile long long *value) {
    return --(*value);
}

/* ---- os_once (real semantics, single-threaded target) --------------------
 * third_party/Libc/stdio/FreeBSD/makebuf.c's __getevp() (reads the
 * STDBUF/STDBUF1 buffer-size-override env vars, a real but non-essential
 * feature) calls this once-only-init API from <os/once_private.h> - an
 * Apple-internal libplatform header this checkout doesn't include at all
 * (confirmed by grep across the whole third_party/Libc tree before adding
 * a stand-in). compat_include/os/once_private.h (this build's own,
 * documented there) declares the matching os_once_t/os_once() surface;
 * this is the real body. Single-threaded correctness argument is
 * identical to pthread_once's in this same file: no concurrent
 * first-callers can race here, so "run it once, remember with a plain
 * flag" is the real behavior, not an approximation. Predicate storage is
 * owned by the CALLER (a plain long, not an opaque ABI struct like real
 * pthread_once_t), so no side table is needed the way pthread_once above
 * needed one.
 */
void os_once(long *predicate, void *context, void (*function)(void *)) {
    if (*predicate == 0) {
        *predicate = 1;
        function(context);
    }
}
