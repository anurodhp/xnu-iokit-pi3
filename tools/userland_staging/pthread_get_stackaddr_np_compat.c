/* DAR-110: built into libsystem_pthread.dylib (build_libsystem_pthread.sh),
 * the real home of both functions below. It used to be compiled only into
 * libdispatch.dylib, which left pthread_get_stackaddr_np exported by the
 * wrong image and pthread_get_stacksize_np exported by none.
 */
/* Real, narrow extraction of third_party/libpthread/src/pthread.c's
 * own pthread_get_stackaddr_np() (that whole file is deliberately never
 * compiled by this project's build_libpthread.sh -- see that script's
 * own header comment -- so individual real functions get pulled out one
 * at a time as needed, same precedent as pthread_main_thread_bootstrap.c/
 * libpthread_attr_compat.c already established for other pieces of the
 * same file).
 *
 * Real Apple body:
 *   void *pthread_get_stackaddr_np(pthread_t t) {
 *       if (t == pthread_self() || t == main_thread())
 *           return t->stackaddr;
 *       if (!_pthread_validate_thread_and_list_lock(t))
 *           return (void *)(uintptr_t)ESRCH;
 *       void *addr = t->stackaddr;
 *       _pthread_lock_unlock(&_pthread_list_lock);
 *       return addr;
 *   }
 * Narrowed to `return t->stackaddr;` unconditionally, not reimplementing
 * the main_thread()/_pthread_validate_thread_and_list_lock() cross-thread
 * liveness check, for two real, checked reasons, not a shortcut:
 *   1. _pthread_validate_thread_and_list_lock/_pthread_list_lock/
 *      main_thread() are themselves internal to pthread.c and were never
 *      extracted either -- reproducing the real check would mean
 *      reproducing __pthread_head list-walking too.
 *   2. More importantly, that check would be actively WRONG for this
 *      project's real state, not just unavailable: pthread_main_thread_
 *      bootstrap.c's own header comment already documents that this
 *      project's pthread_create() trampoline does NOT insert child
 *      threads into __pthread_head (a real, separate, still-open gap
 *      that comment names outright). Reproducing the real list-walk
 *      here would make it reject every real worker thread this project
 *      actually creates, succeeding only for the main thread -- a
 *      real behavior regression relative to just returning the field,
 *      not an improvement.
 * Returning t->stackaddr unconditionally is safe here specifically
 * because this project's pthread_s structs are never freed or reused
 * once allocated (same file's own header: "no real thread-exit
 * mechanism" / mmap'd per-thread stacks are leaked, not reclaimed, on
 * exit) -- so there is no use-after-free risk from skipping the
 * liveness check, unlike real Apple's own environment where pthread_t
 * structs and their stacks genuinely can be reclaimed and reused. */
#include <errno.h>
#include "types_internal.h"

void *
pthread_get_stackaddr_np(pthread_t t)
{
	return t->stackaddr;
}

/* pthread.c:77. */
#define DEFAULT_STACK_SIZE (size_t)(512 * 1024)

/* third_party/libpthread/src/pthread.c:952-1011, narrowed the same way as
 * pthread_get_stackaddr_np() above and for the same reasons (no
 * _pthread_validate_thread_and_list_lock(): created threads are not on
 * __pthread_head here). The TARGET_OS_OSX rlimit branch is compiled out
 * on this iOS target in real Apple too, which leaves the size as
 * stackaddr - stackbottom, with real Apple's DEFAULT_STACK_SIZE fallback
 * when it is unknown (<rdar://problem/42588315>). */
size_t
pthread_get_stacksize_np(pthread_t t)
{
	size_t size;

	if (t == NULL) {
		return ESRCH; // XXX bug?
	}
	size = (size_t)((char *)t->stackaddr - (char *)t->stackbottom);
	return size ? size : DEFAULT_STACK_SIZE;
}
