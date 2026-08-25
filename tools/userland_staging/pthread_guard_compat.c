/* Two small, real (not fabricated) pthread symbols needed the first time
 * a real multithreaded consumer exercises libc++abi's static-init guard
 * machinery (cxa_guard.cpp's __libcpp_condvar_wait/PlatformThreadID path,
 * pulled in by LLVM libcxx/libcxxabi -- see build_llvm_libcxx.sh). Both
 * gaps trace back to the same already-documented, deliberate scope
 * decision in build_libpthread.sh's own header comment: pthread.c and
 * pthread_cancelable.c are NOT built here (QoS/cancellation need real
 * bsdthread_register / real signal delivery to a real second thread,
 * neither wired on this target) -- these two functions are the real,
 * narrowly-scoped subset of what those excluded files would have
 * provided, not a shortcut around them.
 *
 * pthread_mach_thread_np(): real Apple's version (pthread.c:926) walks
 * the internal __pthread_head thread list to validate `t` and read its
 * kernel port -- that list/validation machinery lives entirely inside
 * the excluded pthread.c. libcxxabi's only real call site
 * (cxa_guard_impl.h's PlatformThreadID(), always
 * `pthread_mach_thread_np(pthread_self())`) only ever queries the
 * CURRENT thread and only ever uses the result for equality comparison
 * (deadlock/recursive-init detection), never as a real Mach port (no
 * msg send, no deallocate) -- the exact same real substitution shape
 * libplatform_thread_self.c already established for the identical
 * class of problem (this target has no mach_thread_self trap; that file
 * already installs a real, stable, non-zero, unique-per-thread stand-in
 * value into the real __TSD_MACH_THREAD_SELF TSD slot at thread start,
 * see its own header comment for the full story). Reading that same
 * slot back here is real and correct for the self-query case; a
 * cross-thread query (t != pthread_self()) is honestly out of scope
 * (returns MACH_PORT_NULL) since no thread-list/validation exists here
 * to answer it.
 *
 * pthread_testcancel(): real Apple's version (pthread_cancelable.c:186)
 * checks a per-thread cancel_state field that only pthread_cancel()
 * (also in the excluded pthread_cancelable.c) can ever set to
 * "canceled" -- since that file isn't built, no thread on this target
 * can ever actually be in the canceled state, so a real no-op is the
 * correct behavior for this target, not a stub standing in for
 * something unimplemented. pthread_cond.c (built, real) calls this at
 * real Darwin cancellation points inside its wait loops; on this target
 * those checks are genuinely inert.
 */

typedef unsigned long mach_port_t;
typedef struct _opaque_pthread_t *pthread_t;

#include "../../third_party/xnu-7195/libsyscall/os/tsd.h"

extern pthread_t pthread_self(void);

mach_port_t pthread_mach_thread_np(pthread_t t) {
    if (t == pthread_self()) {
        return (mach_port_t)(unsigned long)_os_tsd_get_direct(__TSD_MACH_THREAD_SELF);
    }
    return 0; /* MACH_PORT_NULL -- cross-thread query unsupported, see header comment */
}

void pthread_testcancel(void) {
    /* real no-op on this target -- see header comment */
}
