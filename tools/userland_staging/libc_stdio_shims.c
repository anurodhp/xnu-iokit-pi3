/* Real-signature stand-ins for the pthread mutex/once and OSAtomic64 API
 * surface third_party/Libc/stdio/FreeBSD/findfp.c (and the rest of the
 * real Apple stdio FILE* implementation) calls to guard its global
 * stream table (__sglue/__sF) and per-FILE `_extra->fl_mutex`.
 *
 * pthread_mutex_init/lock/trylock/unlock/destroy used to be single-
 * threaded no-op bodies here (this file's own prior header comment:
 * "Real mutex/atomics needed only once/if this target ever gets a real
 * libpthread port - tracked as separate future Phase 4 work, not
 * silently faked here"). That future landed 2026-08-24 (real
 * pthread_create()/contended-mutex-under-ulock, see
 * pthread_main_thread_bootstrap.c) -- libpthread.a now exports real
 * bodies for all five, and linking both archives together (every
 * consumer needs libc_stdio.a; several, like ping/bash, now also need
 * real libpthread.a) hit a real duplicate-symbol link error, confirmed
 * by actually linking, not guessed preemptively. Removed here rather
 * than left stale -- standing project rule: when a dependency goes
 * stub -> real, sweep for and fix stale-assumption code instead of
 * leaving a shadowing copy.
 *
 * pthread_once() is untouched (still the single-threaded stand-in a few
 * lines below) -- that symbol was never duplicated by libpthread.a.
 *
 * Types (pthread_mutex_t/pthread_once_t/pthread_mutexattr_t) come from
 * the real Xcode-12 iPhoneOS SDK's <sys/_pthread/_pthread_types.h> via
 * -I $PTHREAD/include already in this build's CFLAGS.
 */

#include <pthread.h>

/* Underscore-prefixed private aliases -
 * third_party/Libc/stdio/FreeBSD/_flock_stub.c (the real flockfile()/
 * funlockfile()/ftrylockfile() public-API implementation, distinct from
 * the FLOCKFILE()/FUNLOCKFILE() internal macros stdio_flockfile_compat.h
 * stands in for) calls these instead of the public pthread_mutex_*()
 * names. Real Apple's own _pthread_mutex_lock etc. are genuine aliases
 * of the public functions on real Darwin too, not a separate
 * implementation -- now that the public names are real (see above),
 * these forward to them for real instead of independently no-op'ing. */
extern int pthread_mutex_lock(pthread_mutex_t *m);
extern int pthread_mutex_trylock(pthread_mutex_t *m);
extern int pthread_mutex_unlock(pthread_mutex_t *m);
extern int pthread_mutex_destroy(pthread_mutex_t *m);

int _pthread_mutex_lock(void *m) {
    return pthread_mutex_lock((pthread_mutex_t *)m);
}

int _pthread_mutex_trylock(void *m) {
    return pthread_mutex_trylock((pthread_mutex_t *)m);
}

int _pthread_mutex_unlock(void *m) {
    return pthread_mutex_unlock((pthread_mutex_t *)m);
}

int _pthread_mutex_destroy(void *m) {
    return pthread_mutex_destroy((pthread_mutex_t *)m);
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

/* OSAtomicIncrement64/OSAtomicDecrement64 no-op stand-ins RETIRED
 * 2026-08-14: tools/userland_staging/build_libplatform.sh now builds real
 * Apple libplatform (third_party/libplatform/src/os/atomic.c provides the
 * genuine OSAtomicIncrement64/OSAtomicDecrement64, real C11-atomics-backed,
 * not this file's own ++/-- stand-ins) and every stdio smoketest links
 * against it - keeping both here is a real duplicate-symbol link error,
 * not a style choice (found by actually linking, not guessed). See
 * build_libplatform.sh's own header comment for what real work that tier
 * took.
 *
 * ---- os_once (real semantics) --------------------------------------------
 * third_party/Libc/stdio/FreeBSD/makebuf.c's __getevp() (reads the
 * STDBUF/STDBUF1 buffer-size-override env vars, a real but non-essential
 * feature) calls this once-only-init API from <os/once_private.h> - an
 * Apple-internal libplatform header this checkout doesn't include at all
 * (confirmed by grep across the whole third_party/Libc tree before adding
 * a stand-in). compat_include/os/once_private.h (this build's own,
 * documented there) declares the matching os_once_t/os_once() surface;
 * this is the real body. Predicate storage is owned by the CALLER (a
 * plain long, not an opaque ABI struct like real pthread_once_t), so no
 * side table is needed the way pthread_once above needed one.
 *
 * REAL BUG FOUND AND FIXED 2026-09-03 (DAR-227): this used to be a plain
 * `if (*predicate == 0) { *predicate = 1; function(context); }`,
 * justified as "identical [reasoning] to pthread_once's in this same
 * file" - but that pthread_once was ALREADY redirected above (this
 * file's own comment, "pthread_once() itself is NOT redefined here") to
 * pthread_tsd_shim.c's real implementation, which got the real atomic
 * fix for this exact bug class as part of this same ticket. The citation
 * this function's old comment pointed at had already gone stale one file
 * over; this is the propagation-by-citation failure mode DAR-227 warned
 * about, one hop removed. Same real fix: a 3-state gate (not-yet /
 * in-progress / done) with proper acquire/release pairing, matching
 * pthread_tsd_shim.c's pthread_once() - see that function for the full
 * reasoning on why a spin instead of a real lock/park. */
#define OS_ONCE_NOT_YET       0
#define OS_ONCE_INPROGRESS    1
#define OS_ONCE_DONE          2

void os_once(long *predicate, void *context, void (*function)(void *)) {
    for (;;) {
        long v = __atomic_load_n(predicate, __ATOMIC_ACQUIRE);
        if (v == OS_ONCE_DONE) {
            return;
        }
        if (v == OS_ONCE_INPROGRESS) {
            __asm__ __volatile__("yield" ::: "memory");
            continue;
        }
        long expected = v;
        if (__atomic_compare_exchange_n(predicate, &expected, OS_ONCE_INPROGRESS,
                                        0 /* strong */, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            function(context);
            __atomic_store_n(predicate, OS_ONCE_DONE, __ATOMIC_RELEASE);
            return;
        }
    }
}
