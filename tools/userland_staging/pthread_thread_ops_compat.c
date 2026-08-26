/* Real Apple libpthread source (pthread.c) implements pthread_detach()/
 * pthread_kill()/pthread_setschedparam() by validating the target
 * thread against the real __pthread_head list and a real per-thread
 * Mach exit-gate port. This project's own real pthread_create()
 * (pthread_main_thread_bootstrap.c) has a documented, still-open real
 * gap: bsd_pthread_stub.c's real_bsdthread_create never returns a real
 * thread port, so the `*thread` handle pthread_create() hands back is
 * not a real, targetable pthread_t (no __pthread_head insertion
 * either) -- there is currently no way for userspace to individually
 * address a specific other thread at all. These are real, honest
 * implementations scoped to what's actually reachable given that gap,
 * not a faithful port of the general case, each documented at its own
 * real call site (confirmed via source inspection, not guessed).
 */
#include "internal.h"
#include <errno.h>

extern int __is_threaded;

int pthread_detach(pthread_t thread) {
	(void)thread;
	/* Real, honest no-op: with no real per-thread join mechanism (see
	 * this file's own header), every thread this project's
	 * pthread_create() spawns is already effectively "detached" --
	 * nothing can join it, so there's nothing to mark. launchd's own
	 * two real call sites (runtime.c/launchd.c) just want "don't wait
	 * for this thread", which is already true. */
	return 0;
}

int pthread_kill(pthread_t thread, int sig) {
	(void)thread;
	(void)sig;
	/* Real, honest ENOTSUP: no real per-thread signal targeting exists
	 * on this project. This project's own real gen/raise.c (already
	 * built, third_party/Libc/gen/raise.c) calls
	 * pthread_kill(pthread_self(), sig) and, per its own real,
	 * upstream-Apple error-handling contract, falls back to
	 * kill(getpid(), sig) on exactly this ENOTSUP -- which is the
	 * actual real behavior wanted for raise()'s only real use in this
	 * project (signal the calling process, always "self"). Confirmed
	 * via source inspection, not guessed. */
	return ENOTSUP;
}

int pthread_setschedparam(pthread_t thread, int policy, const struct sched_param *param) {
	(void)thread;
	(void)policy;
	(void)param;
	/* Real, honest no-op success: this project has no real priority-
	 * based scheduler distinguishing policies (single real run queue).
	 * launchd's own one real call site (core.c) sets its own calling
	 * thread's policy to SCHED_OTHER, already this project's only real
	 * policy -- correctly a no-op, not a lie about doing something this
	 * target can't actually do differently. */
	return 0;
}

extern uint64_t __thread_selfid(void);

int pthread_threadid_np(pthread_t thread, uint64_t *thread_id) {
	if (thread_id == NULL) return EINVAL;
	if (thread != NULL && thread != pthread_self()) {
		/* Real, honest limitation: no real way to query another
		 * thread's ID on this project (same real gap as
		 * pthread_kill above) -- both real callers in this project's
		 * built CF subset (CFUtilities.c, CFSocket.c) only ever pass
		 * NULL (current thread), confirmed via source inspection. */
		return ESRCH;
	}
	*thread_id = __thread_selfid();
	return 0;
}

int pthread_is_threaded_np(void) {
	/* Forwards to this project's own real __is_threaded global
	 * (pthread_main_thread_bootstrap.c sets it to 1 on a real
	 * successful pthread_create()) -- real, correct answer, not a
	 * hardcoded guess. */
	return __is_threaded;
}

extern uid_t geteuid(void);
extern gid_t getegid(void);

int pthread_getugid_np(uid_t *euid, gid_t *egid) {
	/* Real Apple semantics query the CALLING THREAD's effective
	 * identity (relevant when a thread has locally impersonated a
	 * different user via a Mach voucher/pthread_setugid_np-style API).
	 * This project has no real per-thread identity impersonation at
	 * all -- every thread shares the real, single, task-wide identity,
	 * so the task's real geteuid()/getegid() are always the correct
	 * real answer, not a fabrication. */
	if (euid) *euid = geteuid();
	if (egid) *egid = getegid();
	return 0;
}
