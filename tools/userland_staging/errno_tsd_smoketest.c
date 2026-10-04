/*
 * errno_tsd_smoketest.c -- errno is per thread, through the pthread TSD slot, and a timed condition-variable wait that
 * times out says ETIMEDOUT.
 *
 * cerror_stub.c used to give every thread one plain global `errno`. libpthread reads errno the way real Apple's code does,
 * through TSD slot 1 (an int* libpthread installs per thread), so _pthread_psynch_cond_wait saw whatever stale value sat in
 * that cell instead of the ETIMEDOUT (0x13c: ETIMEDOUT | the kernel's "cv cleared" bit) the kernel had returned, and
 * pthread_cond_timedwait() reported EINVAL for a plain timeout. GLib's g_cond_wait_until turns that into
 * g_thread_abort("pthread_cond_timedwait"), which killed WebKit's UI process on the Pi.
 *
 * Checks: (1) &errno equals this thread's TSD slot 1 and the two are different cells on two threads, (2) a write through
 * errno on one thread is not seen by the other, (3) pthread_cond_timedwait that really times out returns ETIMEDOUT, for a
 * statically and a dynamically initialised mutex/cond.
 */
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>

static int fails;
#define CHECK(what, cond) do { int ok_ = !!(cond); printf("%s %s\n", ok_ ? "PASS" : "FAIL", what); if (!ok_) fails++; } while (0)

static int *tsd_slot1(void) {
	uintptr_t tsd;
	__asm__("mrs %0, TPIDRRO_EL0" : "=r"(tsd));
	tsd &= ~(uintptr_t)7;
	return tsd ? (int *)((uintptr_t *)tsd)[1] : 0;
}

static int *main_cell, *worker_cell;
static int worker_saw_main_value;

static void *worker(void *arg) {
	(void)arg;
	errno = 11;
	worker_cell = &errno;
	CHECK("worker: &errno is its TSD slot 1", &errno == tsd_slot1());
	worker_saw_main_value = (errno == 4242);
	return 0;
}

static struct timespec abs_in_ms(long ms) {
	struct timeval tv; struct timespec t;
	gettimeofday(&tv, 0);
	t.tv_sec = tv.tv_sec; t.tv_nsec = tv.tv_usec * 1000 + ms * 1000000;
	while (t.tv_nsec >= 1000000000) { t.tv_sec++; t.tv_nsec -= 1000000000; }
	return t;
}

static pthread_mutex_t smtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t scond = PTHREAD_COND_INITIALIZER;

int main(void) {
	pthread_t t;
	errno = 4242;
	main_cell = &errno;
	CHECK("main: &errno is its TSD slot 1", main_cell == tsd_slot1());
	pthread_create(&t, 0, worker, 0);
	pthread_join(t, 0);
	CHECK("errno cells of two threads differ", main_cell != worker_cell);
	CHECK("a write on one thread is not seen on the other", !worker_saw_main_value && errno == 4242);

	struct timespec ab = abs_in_ms(20);
	pthread_mutex_lock(&smtx);
	CHECK("timed-out pthread_cond_timedwait (static init) is ETIMEDOUT", pthread_cond_timedwait(&scond, &smtx, &ab) == ETIMEDOUT);
	pthread_mutex_unlock(&smtx);

	pthread_mutex_t m; pthread_cond_t c;
	pthread_mutex_init(&m, 0); pthread_cond_init(&c, 0);
	ab = abs_in_ms(20);
	pthread_mutex_lock(&m);
	CHECK("timed-out pthread_cond_timedwait (pthread_*_init) is ETIMEDOUT", pthread_cond_timedwait(&c, &m, &ab) == ETIMEDOUT);
	pthread_mutex_unlock(&m);

	printf(fails ? "RESULT FAIL\n" : "RESULT OK\n");
	return fails != 0;
}
