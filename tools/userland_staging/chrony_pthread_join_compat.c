#include <errno.h>
#include <pthread.h>

/* Real, documented gap -- NOT a general-purpose pthread_join().
 *
 * tools/userland_staging/pthread_main_thread_bootstrap.c's own header
 * comment (the real pthread_create() reimplementation this project
 * uses, since real Apple pthread.c is deliberately never compiled here)
 * states the underlying fact plainly: bsd_pthread_stub.c's
 * real_bsdthread_create() always sets *retval = 0 (no real thread port
 * is created/returned), so the `pthread_t` handed back is a bare
 * kernel-facing value, not a real struct pthread_s -- "there is still
 * no way for userspace to pthread_join()/pthread_kill()/individually
 * terminate the new thread; it only stops when the whole task exits."
 * A real pthread_join() needs a real per-thread join primitive (a real
 * thread port + real bsdthread_create bookkeeping) that does not exist
 * in this project yet -- a kernel-level follow-on, not something a
 * userland compat shim can honestly fix.
 *
 * This project's one real caller, chrony's nameserv_async.c
 * end_resolving(), is safe against this stub specifically (not in
 * general) because of its own real ordering: SCH_AddFileHandler() only
 * invokes end_resolving() once inst->pipe[0] has become readable, and
 * the ONLY writer of that pipe is start_resolving() itself, at the very
 * end of its body, after inst->status is fully set. By the time
 * pthread_join() is reached here, the spawned thread has therefore
 * already run to completion in every way that matters to the caller --
 * the pipe write is the real synchronization event, pthread_join() is
 * redundant with it at this specific call site. Returning success
 * without a real wait is therefore accurate here, not papered over.
 *
 * Do NOT reuse this for a future caller that does not have its own
 * independent real completion signal -- that would be a real,
 * un-synchronized race, not this one's situation. */
int
pthread_join(pthread_t thread, void **value_ptr)
{
  (void)thread;
  if (value_ptr)
    *value_ptr = NULL;
  return 0;
}
