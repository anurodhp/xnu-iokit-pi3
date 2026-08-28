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
#include "types_internal.h"

void *
pthread_get_stackaddr_np(pthread_t t)
{
	return t->stackaddr;
}
