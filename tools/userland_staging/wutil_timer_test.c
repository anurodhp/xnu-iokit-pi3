/*
 * wutil_timer_test (DAR-432): libWUtil timer queue across wall-clock steps,
 * and leeway coalescing. Needs root for the settimeofday() cases (skipped
 * otherwise). All durations are measured on mach_absolute_time, which a
 * clock step does not move. The clock is always put back afterwards.
 *
 *  1 baseline: a 300 ms timer fires within 290..600 ms
 *  2 backward step: a 600 ms timer armed, the wall clock set back 60 s
 *    after 150 ms: must still fire at 600 ms (450..900 allowed for QEMU jitter). A wall-clock
 *    queue waits about 60 s (the case gives up at 3000 ms)
 *  3 forward step: same with +60 s. A wall-clock queue fires at once
 *    (about 150 ms) instead of 600 ms
 *  4 persistent 100 ms timer over a 1 s window with a -60 s step in the
 *    middle: 8..11 fires
 *  5 leeway (needs WMAddTimerHandlerWithLeeway): A = 300 ms with leeway
 *    400 ms, B = 600 ms with none. Both fire in one wake-up near 600 ms
 *    when the kernel timer is in use (A late by less than its leeway plus
 *    150 ms); under WM_EVENT_BACKEND=select A fires at 300 ms
 * Exit status 0 means every case passed.
 * `wutil_timer_test step SECS` only moves the wall clock by SECS and exits.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/time.h>
#include <mach/mach_time.h>

typedef void WMCallback(void *clientData);
typedef void WMInputProc(int fd, int mask, void *clientData);
typedef void *WMHandlerID;
extern WMHandlerID WMAddTimerHandler(int ms, WMCallback *cb, void *cd);
extern WMHandlerID WMAddPersistentTimerHandler(int ms, WMCallback *cb, void *cd);
extern void WMDeleteTimerHandler(WMHandlerID id);
extern WMHandlerID WMAddInputHandler(int fd, int cond, WMInputProc *proc, void *cd);
extern int W_HandleInputEvents(int waitForInput, int inputfd);
extern void W_CheckTimerHandlers(void);
enum { WIReadMask = 1 };

static int failures;
static mach_timebase_info_data_t tb;

static void say(const char *s) { write(1, s, strlen(s)); }

static void result(const char *name, int ok, const char *extra)
{
	char b[300];
	snprintf(b, sizeof b, "%-4s %s%s%s\n", ok ? "PASS" : "FAIL", name, extra ? "  " : "", extra ? extra : "");
	say(b);
	if (!ok)
		failures++;
}

static long mono_ms(void)
{
	uint64_t a = mach_absolute_time();

	return (long)(((a / tb.denom) * tb.numer + (a % tb.denom) * tb.numer / tb.denom) / 1000000);
}

static int step_ok = 1;
static long wall_delta;		/* net seconds added to the wall clock */

static int wall_step(long secs)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	tv.tv_sec += secs;
	if (settimeofday(&tv, NULL) != 0) {
		step_ok = 0;
		return -1;
	}
	wall_delta += secs;
	return 0;
}

static void wall_restore(void)
{
	if (wall_delta)
		wall_step(-wall_delta);
}

static void on_input(int fd, int mask, void *cd) { }

static long t0, fired_at[4];
static int fired[4], nfires;
static long step_secs;

static void cb_mark(void *cd)
{
	int i = (int)(long)cd;
	fired[i]++;
	fired_at[i] = mono_ms() - t0;
}

static void cb_step(void *cd) { wall_step(step_secs); }
static void cb_count(void *cd) { nfires++; }

static int wakes;

/* pump the queue like WMNextEvent: check timers, block until the next event */
static void pump(int *cond, long limit_ms)
{
	while (!*cond && mono_ms() - t0 < limit_ms) {
		W_CheckTimerHandlers();
		if (*cond)
			break;
		W_HandleInputEvents(1, -1);
		wakes++;
	}
	W_CheckTimerHandlers();
}

static void pump_all_fired(int n, long limit_ms)
{
	for (;;) {
		int i, all = 1;
		for (i = 0; i < n; i++)
			all &= fired[i] != 0;
		if (all || mono_ms() - t0 >= limit_ms)
			return;
		W_CheckTimerHandlers();
		for (i = 0, all = 1; i < n; i++)
			all &= fired[i] != 0;
		if (all)
			return;
		W_HandleInputEvents(1, -1);
		wakes++;
	}
}

static void stepcase(const char *name, long secs)
{
	char msg[200];
	WMHandlerID mark;

	memset(fired, 0, sizeof fired);
	step_secs = secs;
	t0 = mono_ms();
	mark = WMAddTimerHandler(600, cb_mark, (void *)0);
	WMAddTimerHandler(150, cb_step, NULL);
	pump_all_fired(1, 3000);
	wall_restore();
	if (!fired[0])
		WMDeleteTimerHandler(mark);	/* a stuck timer must not leak into the next case */
	if (!fired[0])
		snprintf(msg, sizeof msg, "(not fired within 3000 ms; want 450..900)");
	else
		snprintf(msg, sizeof msg, "(fired at %ld ms; want 450..900)", fired_at[0]);
	result(name, fired[0] && fired_at[0] >= 450 && fired_at[0] <= 900, msg);
}

int main(int argc, char **argv)
{
	int p[2], i;
	char msg[300];
	WMHandlerID (*addLeeway)(int, int, WMCallback *, void *);

	mach_timebase_info(&tb);
	if (argc == 3 && strcmp(argv[1], "step") == 0) {
		/* wutil_timer_test step SECS: move the wall clock (for stepping it under a running wmaker) */
		struct timeval tv;

		gettimeofday(&tv, NULL);
		tv.tv_sec += atol(argv[2]);
		return settimeofday(&tv, NULL) != 0;
	}
	pipe(p);
	WMAddInputHandler(p[0], WIReadMask, on_input, NULL);	/* a never-ready fd keeps the wait alive */

	/* 1 */
	memset(fired, 0, sizeof fired);
	t0 = mono_ms();
	WMAddTimerHandler(300, cb_mark, (void *)0);
	pump_all_fired(1, 3000);
	snprintf(msg, sizeof msg, "(%ld ms)", fired_at[0]);
	result("1 baseline 300 ms timer", fired[0] && fired_at[0] >= 290 && fired_at[0] <= 600, msg);

	if (geteuid() != 0 || wall_step(0) != 0) {
		say("SKIP 2-4 (settimeofday needs root)\n");
	} else {
		stepcase("2 wall clock stepped back 60 s", -60);
		stepcase("3 wall clock stepped forward 60 s", 60);

		/* 4 */
		nfires = 0;
		step_secs = -60;
		t0 = mono_ms();
		{
			WMHandlerID pid = WMAddPersistentTimerHandler(100, cb_count, NULL);
			int done = 0;
			WMAddTimerHandler(500, cb_step, NULL);
			while (mono_ms() - t0 < 1000) {
				W_CheckTimerHandlers();
				W_HandleInputEvents(1, -1);
			}
			(void)done;
			WMDeleteTimerHandler(pid);
		}
		wall_restore();
		snprintf(msg, sizeof msg, "(%d fires in 1000 ms; want 8..11)", nfires);
		result("4 persistent timer over a -60 s step", nfires >= 8 && nfires <= 11, msg);
	}

	/* 5 */
	addLeeway = (WMHandlerID (*)(int, int, WMCallback *, void *))dlsym(RTLD_DEFAULT, "WMAddTimerHandlerWithLeeway");
	if (!addLeeway) {
		say("SKIP 5 leeway (WMAddTimerHandlerWithLeeway not in this libWUtil)\n");
	} else {
		int select_backend = getenv("WM_EVENT_BACKEND") != NULL;
		memset(fired, 0, sizeof fired);
		wakes = 0;
		t0 = mono_ms();
		addLeeway(300, 400, cb_mark, (void *)0);
		WMAddTimerHandler(600, cb_mark, (void *)1);
		/* default leeway for a 600 ms timer is 50 ms */
		pump_all_fired(2, 3000);
		snprintf(msg, sizeof msg, "(A at %ld ms, B at %ld ms, %d blocking wake-ups; %s)",
			 fired_at[0], fired_at[1], wakes, select_backend ? "select: A at 300" : "kernel timer: A with B");
		if (select_backend)
			result("5 leeway", fired[0] && fired[1] && fired_at[0] >= 290 && fired_at[0] <= 450 && fired_at[1] <= 900, msg);
		else
			result("5 leeway coalesced", fired[0] && fired[1] && fired_at[0] >= 290 && fired_at[0] <= 900 &&
			       fired_at[1] >= 550 && fired_at[1] <= 900 && wakes <= 2, msg);
	}

	wall_restore();
	(void)i;
	say(failures ? "wutil_timer_test: FAILED\n" : "wutil_timer_test: all passed\n");
	return failures != 0;
}
