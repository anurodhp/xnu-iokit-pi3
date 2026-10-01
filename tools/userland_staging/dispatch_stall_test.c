/*
 * dispatch_stall_test.c (DAR-409): functional test of libdispatch's
 * thread-pool stall monitor (src/event/workqueue.c,
 * _dispatch_workq_monitor_pools / _dispatch_workq_count_runnable_workers).
 *
 * This port builds libdispatch with DISPATCH_USE_INTERNAL_WORKQUEUE=1, so a
 * non-overcommit global root queue is capped at dispatch_hw_config(active_cpus)
 * threads (src/queue.c, _dispatch_root_queue_init_pthread_pool). Without the
 * monitor, a worker parked on that queue starves it for good. Tests:
 *   normal    many short blocks, dispatch_apply, dispatch_after, serial queue
 *             with a timer source: the monitor must not break these.
 *   stallsem  park <cap> workers on a semaphore on the DEFAULT-QoS
 *             (non-overcommit) global queue, then dispatch_async a short
 *             block there: must run within 3 s (FAIL = DAR-409 bug).
 *   stallslp  same, parked in sleep().
 *   idle      sit idle <n> s with the pool live and report the process's
 *             CPU time (the monitor timer must not busy-wake).
 *   forever   syslogd/database_server pattern: <cap> workers parked for good,
 *             then 20 short blocks on the same queue must all run.
 * Usage: dispatch_stall_test [all|normal|stallsem|stallslp|idle|forever] [idle_secs]
 * Exit status 0 iff every selected test passed.
 */
#include <dispatch/dispatch.h>
#include <sys/qos.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define STALL_TIMEOUT_S 3

/* monotonic (mach_absolute_time, the same clock as dispatch_time(NOW)): chronyd
 * may step the wall clock during a boot test, which a gettimeofday-based
 * latency would report as a bogus multi-minute number. */
static double now_s(void) {
	static mach_timebase_info_data_t tb;
	if (!tb.denom) mach_timebase_info(&tb);
	return (double)mach_absolute_time() * tb.numer / tb.denom / 1e9;
}
static double cpu_s(void) {
	struct rusage ru; getrusage(RUSAGE_SELF, &ru);
	return ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6 +
			ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6;
}
static int nthreads(void) {
	thread_act_array_t t; mach_msg_type_number_t n = 0;
	if (task_threads(mach_task_self(), &t, &n) != KERN_SUCCESS) return -1;
	return (int)n;
}
static int active_cpus(void) {
	int v = 0; size_t l = sizeof(v);
	if (sysctlbyname("hw.activecpu", &v, &l, NULL, 0) != 0 || v < 1) v = 1;
	return v;
}
static dispatch_queue_t gq(void) {
	return dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0);
}

static int test_normal(void) {
	double t0 = now_s();
	dispatch_group_t g = dispatch_group_create();
	__block volatile int count = 0;
	for (int i = 0; i < 2000; i++)
		dispatch_group_async(g, gq(), ^{ __sync_fetch_and_add(&count, 1); });
	int r1 = dispatch_group_wait(g, dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_SEC));
	__block volatile int applied = 0;
	dispatch_apply(1000, gq(), ^(size_t i) { (void)i; __sync_fetch_and_add(&applied, 1); });
	dispatch_semaphore_t sa = dispatch_semaphore_create(0);
	double ta = now_s();
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 200 * NSEC_PER_MSEC), gq(), ^{
		dispatch_semaphore_signal(sa); });
	int r2 = dispatch_semaphore_wait(sa, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
	double after_lat = now_s() - ta;
	dispatch_queue_t sq = dispatch_queue_create("stall.test.serial", NULL);
	dispatch_semaphore_t ss = dispatch_semaphore_create(0);
	__block int ticks = 0;
	dispatch_source_t tm = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, sq);
	dispatch_source_set_timer(tm, dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC),
			100 * NSEC_PER_MSEC, 10 * NSEC_PER_MSEC);
	dispatch_source_set_event_handler(tm, ^{
		if (++ticks == 5) dispatch_semaphore_signal(ss); });
	dispatch_resume(tm);
	int r3 = dispatch_semaphore_wait(ss, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
	dispatch_source_cancel(tm);
	int ok = !r1 && count == 2000 && applied == 1000 && !r2 && !r3;
	printf("normal: blocks=%d apply=%d after=%s(%.2fs) timer=%s(ticks=%d) total=%.2fs threads=%d -> %s\n",
			count, applied, r2 ? "TIMEOUT" : "ok", after_lat, r3 ? "TIMEOUT" : "ok",
			ticks, now_s() - t0, nthreads(), ok ? "PASS" : "FAIL");
	return ok;
}

/* park `cap` workers on the default non-overcommit global queue, then probe */
static int stall_common(const char *name, int use_sleep, int forever) {
	int cap = active_cpus();
	dispatch_semaphore_t started = dispatch_semaphore_create(0);
	dispatch_semaphore_t release = dispatch_semaphore_create(0);
	dispatch_semaphore_t parked_done = dispatch_semaphore_create(0);
	for (int i = 0; i < cap; i++) {
		dispatch_async(gq(), ^{
			dispatch_semaphore_signal(started);
			if (forever) {
				dispatch_semaphore_wait(release, DISPATCH_TIME_FOREVER);
			} else if (use_sleep) {
				sleep(8);
			} else {
				dispatch_semaphore_wait(release, DISPATCH_TIME_FOREVER);
			}
			dispatch_semaphore_signal(parked_done);
		});
	}
	for (int i = 0; i < cap; i++) {
		if (dispatch_semaphore_wait(started, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC))) {
			printf("%s: parked worker %d never started -> FAIL\n", name, i);
			return 0;
		}
	}
	int nprobe = forever ? 20 : 1;
	dispatch_semaphore_t probe = dispatch_semaphore_create(0);
	double t0 = now_s();
	for (int i = 0; i < nprobe; i++)
		dispatch_async(gq(), ^{ dispatch_semaphore_signal(probe); });
	int ran = 0;
	for (int i = 0; i < nprobe; i++) {
		if (dispatch_semaphore_wait(probe, dispatch_time(DISPATCH_TIME_NOW,
				(int64_t)STALL_TIMEOUT_S * NSEC_PER_SEC - (int64_t)((now_s() - t0) * 1e9)))) break;
		ran++;
	}
	double lat = now_s() - t0;
	int ok = (ran == nprobe);
	if (ok)
		printf("%s: cap=%d parked=%d, %d probe block(s) ran in %.2fs while parked, threads=%d -> PASS\n",
				name, cap, cap, ran, lat, nthreads());
	else
		printf("%s: cap=%d parked=%d, only %d/%d probe block(s) ran in %ds -> FAIL (DAR-409: pool starved)\n",
				name, cap, cap, ran, nprobe, STALL_TIMEOUT_S);
	if (!forever) {
		for (int i = 0; i < cap; i++) dispatch_semaphore_signal(release);
		for (int i = 0; i < cap; i++)
			dispatch_semaphore_wait(parked_done, dispatch_time(DISPATCH_TIME_NOW, 15 * NSEC_PER_SEC));
		if (!ok) {
			/* the starved probe must now drain, proving the queue was merely stuck */
			int late = !dispatch_semaphore_wait(probe, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
			printf("%s: after releasing the parked workers the probe %s\n", name, late ? "ran" : "STILL did not run");
		}
	}
	return ok;
}

static int test_idle(int secs) {
	/* the pool is live (earlier tests created workers; make sure at least one) */
	dispatch_semaphore_t s = dispatch_semaphore_create(0);
	dispatch_async(gq(), ^{ dispatch_semaphore_signal(s); });
	dispatch_semaphore_wait(s, DISPATCH_TIME_FOREVER);
	sleep(1);
	double c0 = cpu_s(), t0 = now_s();
	sleep(secs);
	double dc = cpu_s() - c0, dt = now_s() - t0;
	int ok = dc < 0.02 * dt + 0.05;   /* well under 2% CPU */
	printf("idle: %.2fs wall, %.3fs CPU (%.2f%%), threads=%d -> %s\n",
			dt, dc, 100 * dc / dt, nthreads(), ok ? "PASS" : "FAIL");
	return ok;
}

int main(int argc, char **argv) {
	const char *m = argc > 1 ? argv[1] : "all";
	int secs = argc > 2 ? atoi(argv[2]) : 10;
	int all = !strcmp(m, "all"), pass = 1;
	setvbuf(stdout, NULL, _IONBF, 0);
	printf("dispatch_stall_test: active_cpus=%d mode=%s\n", active_cpus(), m);
	if (all || !strcmp(m, "normal"))   pass &= test_normal();
	if (all || !strcmp(m, "stallsem")) pass &= stall_common("stallsem", 0, 0);
	if (all || !strcmp(m, "stallslp")) pass &= stall_common("stallslp", 1, 0);
	if (all || !strcmp(m, "idle"))     pass &= test_idle(secs);
	if (all || !strcmp(m, "forever"))  pass &= stall_common("forever", 0, 1);
	printf("dispatch_stall_test: cpu_time=%.3fs %s\n", cpu_s(), pass ? "ALL PASS" : "SOME FAILED");
	_exit(pass ? 0 : 1);
}
