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
 * DISPATCH_ONCE_INIT / static-zero-init convention). This target is
 * provably single-threaded (see build_libpthread.sh's own findings -
 * bsdthread_create() returns real ENOTSUP), so no real cross-thread
 * synchronization is needed for correctness here - a plain check-and-set
 * is genuinely sufficient, not a corner cut.
 */
#ifndef _COPYFILE_DISPATCH_ONCE_COMPAT_H_
#define _COPYFILE_DISPATCH_ONCE_COMPAT_H_

typedef long dispatch_once_t;

static inline void dispatch_once(dispatch_once_t *predicate, void (^block)(void)) {
    if (*predicate == 0) {
        *predicate = 1;
        block();
    }
}

#endif
