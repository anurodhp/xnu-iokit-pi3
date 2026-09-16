/*
 * pthread_cond_smoketest.c -- real mutex + condition-variable handoff
 * probe (DAR-280).
 *
 * Until 2026-09-16 this port's real_bsdthread_register() reported
 * mutex_default_policy=0 to libpthread, which tells libpthread's real
 * _pthread_mutex_init()/_pthread_cond_wait() that ulock-based sync is
 * NOT available - so every pthread_cond_wait() fell through to the
 * PSYNCH path (__psynch_cvwait), and psynch_cvwait/cvsignal/cvbroad/
 * cvclrprepost (plus psynch_mutexwait/psynch_mutexdrop, for a genuinely
 * contended mutex) are all wired to an unconditional ENOTSUP stub in
 * bsd_pthread_stub.c, never implemented. libpthread's real
 * pthread_cond.c doesn't expect that return from the kernel and
 * mishandles it, reading through what ends up being a NULL/garbage
 * pointer - a real QEMU boot put the main thread in
 * _pthread_cond_wait+0x2a8, SIGSEGV on far 0x26.
 *
 * The fix (DAR-280) was to set _PTHREAD_REG_DEFAULT_USE_ULOCK in that
 * same registration data, routing mutex + condvar sync through this
 * port's real, already-QEMU-verified ulock_wait()/ulock_wake() kernel
 * implementation instead. This test is the exact textbook mutex+condvar
 * handoff DAR-280's own report used to reproduce the crash - kept
 * permanently so a regression here can never again be diagnosed from a
 * bare EXC_CORPSE_NOTIFY line with no symbol.
 *
 * Deliberately real Apple static-initializer usage (PTHREAD_MUTEX_
 * INITIALIZER / PTHREAD_COND_INITIALIZER, no explicit *_init() call) -
 * that is the exact form that crashed, and per pthread_cond.c's own
 * comment a statically-initialized condvar only picks its sync mode
 * (ulock vs psynch) on the *first* _cond_wait() call, based on whether
 * the mutex it's paired with is itself ulock-eligible - so this has to
 * be the first and only condvar use in the process for the test to mean
 * anything about the default policy.
 */
#include <stdio.h>
#include <pthread.h>

static pthread_mutex_t done_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  done_cond = PTHREAD_COND_INITIALIZER;
static int child_done;
static int failures;

static void *
child_main(void *arg)
{
	(void)arg;
	printf("pthread_cond_smoketest: mark child-start\n");
	pthread_mutex_lock(&done_lock);
	child_done = 1;
	pthread_cond_signal(&done_cond);
	pthread_mutex_unlock(&done_lock);
	printf("pthread_cond_smoketest: mark child-signaled\n");
	return NULL;
}

int
main(void)
{
	printf("pthread_cond_smoketest: start\n");

	pthread_t th;
	int rc = pthread_create(&th, NULL, child_main, NULL);
	if (rc != 0) {
		printf("pthread_cond_smoketest: FAIL pthread_create rc=%d\n", rc);
		failures++;
		goto done;
	}

	printf("pthread_cond_smoketest: mark main-wait\n");
	pthread_mutex_lock(&done_lock);
	while (!child_done) {
		/* The exact call DAR-280 crashed inside. */
		pthread_cond_wait(&done_cond, &done_lock);
	}
	pthread_mutex_unlock(&done_lock);
	printf("pthread_cond_smoketest: mark main-woke\n");

	if (!child_done) {
		printf("pthread_cond_smoketest: FAIL child_done still 0 after wait\n");
		failures++;
	}

done:
	if (failures == 0) {
		printf("pthread_cond_smoketest: PASS\n");
		return 0;
	}
	printf("pthread_cond_smoketest: FAIL (%d)\n", failures);
	return 1;
}
