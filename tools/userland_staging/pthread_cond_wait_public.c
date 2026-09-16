/* DAR-280: the real public pthread_cond_wait()/pthread_cond_timedwait()
 * entry points, matching third_party/libpthread/src/pthread_cancelable.c
 * (lines ~404-441 of libpthread-454.80.2) - real Apple source, not
 * reinvented, just re-expressed against the PUBLIC <pthread.h> instead of
 * that file's own "internal.h" (which pulls in a large, fragile private
 * header graph - workqueue_private.h/tsd_private.h/etc - that doesn't
 * compile cleanly standalone on this project's freestanding target; the
 * same class of exclusion build_libpthread.sh's own header comment
 * documents for pthread.c itself). The only thing genuinely needed from
 * that private world is the real internal 5-argument implementation
 * (`__pthread_cond_wait` at the linker level) and the real
 * `PTHREAD_CONFORM_UNIX03_NOCANCEL` enumerator value (1, from
 * types_internal.h's `__enum_closed_decl(pthread_conformance_t, unsigned,
 * ...)` - an `unsigned`-backed closed enum, safe to pass as a plain
 * `unsigned` here since that's its real underlying ABI type) - both
 * declared/defined by the already-compiled, already-correct pthread_cond.o
 * (extracted from libpthread.a) this file links against, not redeclared
 * here as a struct or type.
 *
 * That file as a whole isn't compiled into this project's
 * libsystem_pthread.dylib (its other functions - pthread_join, sigwait -
 * pull in _pthread_join()/__sigwait() dependencies not needed here), so
 * build_libsystem_pthread.sh previously substituted a raw
 * `-Wl,-alias,__pthread_cond_wait,_pthread_cond_wait` linker alias instead
 * of a real 2-arg-to-5-arg trampoline.
 *
 * That alias was wrong, not just incomplete: `_pthread_cond_wait` (the
 * internal, 5-argument real implementation in pthread_cond.c) and the
 * public 2-argument `pthread_cond_wait(cond, mutex)` are NOT the same
 * function with a subset of arguments - aliasing them means a caller that
 * only sets up x0 (cond) and x1 (mutex) jumps into code that unconditionally
 * reads x2/x3/x4 as abstime/isRelative/conforming, i.e. whatever garbage
 * those registers happened to hold from the caller's own prior code. This
 * was DAR-280's real root cause: the internal function's `if (abstime) {
 * ... }` guard only skips the timeout-conversion block when abstime is a
 * clean NULL, but a raw alias never sets it to NULL at all, so it reads a
 * garbage stack/register value as a `struct timespec *` and dereferences
 * `abstime->tv_nsec` through it - real, root-caused via a real QEMU crash
 * disassembled back to that exact instruction (`ldr x8, [x8, #0x8]` at
 * __pthread_cond_wait+0x2a8, right after the real `if (abstime)` check
 * that a genuine trampoline call would have made take the NULL-skip path).
 */
#include <pthread.h>
#include <time.h>

extern int _pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex,
		const struct timespec *abstime, int isRelative,
		unsigned conforming);

#define PTHREAD_CONFORM_UNIX03_NOCANCEL 1u

int
pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
	return _pthread_cond_wait(cond, mutex, (const struct timespec *)0, 0,
	           PTHREAD_CONFORM_UNIX03_NOCANCEL);
}

int
pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
		const struct timespec *abstime)
{
	return _pthread_cond_wait(cond, mutex, abstime, 0,
	           PTHREAD_CONFORM_UNIX03_NOCANCEL);
}
