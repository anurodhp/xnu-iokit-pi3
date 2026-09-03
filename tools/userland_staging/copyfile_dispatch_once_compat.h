/* Real, ABI-correct minimal dispatch_once compat shim for
 * xattr_flags.c's one real call site (nameInDefaultList's lazy
 * defaultPropertyTable init). Real GCD/libdispatch is out of scope for
 * this project (see docs/roadmap.md Phase 4's libdispatch entry -
 * queue.c alone is 8000+ lines of OS-integrated internals not separable
 * at file granularity), but dispatch_once_t/dispatch_once() are PUBLIC,
 * stable API with a well-known contract ("run this Block exactly once,
 * thread-safely") - not an internal GCD object layout - so a real,
 * correct, minimal implementation is legitimate last-resort scope here,
 * unlike hand-extracting queue_internal.h's private structs would be.
 * Real public ABI: dispatch_once_t is a plain `long`, caller-allocated,
 * zero-initialized before first use (matching the real
 * DISPATCH_ONCE_INIT / static-zero-init convention).
 *
 * REAL BUG FOUND AND FIXED 2026-09-03 (DAR-227): this used to be a plain
 * `if (*predicate == 0) { *predicate = 1; block(); }`, justified by
 * citing build_libpthread.sh's "bsdthread_create() returns real ENOTSUP"
 * finding BY NAME as proof this target is provably single-threaded. That
 * finding stopped being true on 2026-08-20 (Phase 7.5 step 1) -- see
 * build_libpthread.sh's own corrected header and pthread_tsd_shim.c's
 * DAR-227 fix for the full trail. Citing another file's claim instead of
 * re-deriving it is exactly how this stale assumption spread to seven
 * files instead of one real place; correcting the origin does not reach
 * copies like this one.
 *
 * Fixed by delegating to real `_os_once()`, not by hand-rolling atomics
 * again: `libsystem_platform.dylib` is a real, already-linked sibling of
 * `libcopyfile.dylib` (see this header's own build_copyfile_dylib.sh's
 * final link line) and exports a real, correct `_os_once` (confirmed via
 * `nm -gU libsystem_platform.dylib`) built from this project's pinned,
 * unmodified `third_party/libplatform/src/os/lock.c`. `os_once_t` is
 * layout-identical to `dispatch_once_t` (both a plain, zero-init `long`
 * -- confirmed against `third_party/libplatform/private/os/
 * once_private.h`), so this shim is a thin, real ABI bridge from the
 * public Block-based `dispatch_once()` call shape to the real C-function-
 * pointer-plus-context `_os_once()` underneath, not a reimplementation.
 */
#ifndef _COPYFILE_DISPATCH_ONCE_COMPAT_H_
#define _COPYFILE_DISPATCH_ONCE_COMPAT_H_

typedef long dispatch_once_t;

/* Real libplatform ABI (third_party/libplatform/private/os/once_private.h,
 * src/os/lock.c) -- os_once_t and dispatch_once_t share the same plain
 * `long` layout, so a caller's dispatch_once_t* is passed straight
 * through as an os_once_t*. */
extern void _os_once(dispatch_once_t *predicate, void *ctxt, void (*func)(void *));

static inline void __copyfile_once_trampoline(void *ctxt) {
    void (^block)(void) = (void (^)(void))ctxt;
    block();
}

static inline void dispatch_once(dispatch_once_t *predicate, void (^block)(void)) {
    _os_once(predicate, (void *)block, __copyfile_once_trampoline);
}

#endif
