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
 *    middle: the same number of fires (+-1) as a 1 s window without a step
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
#include <sys/event.h>
#include <sys/syscall.h>
#include <errno.h>

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


/* `wutil_timer_test kq64`: which raw kevent64 EVFILT_TIMER registrations does this kernel accept, and do they fire? */
static int raw_kevent64(int kq, const struct kevent64_s *change, int nch, struct kevent64_s *out, int nout,
			const struct timespec *ts, int *err)
{
	register long x0 __asm__("x0") = kq;
	register long x1 __asm__("x1") = (long)change;
	register long x2 __asm__("x2") = nch;
	register long x3 __asm__("x3") = (long)out;
	register long x4 __asm__("x4") = nout;
	register long x5 __asm__("x5") = 0;
	register long x6 __asm__("x6") = (long)ts;
	register long x16 __asm__("x16") = SYS_kevent64;
	unsigned long failed;

	__asm__ volatile ("svc #0x80\n\tcset %0, cs"
			  : "=r" (failed), "+r" (x0), "+r" (x1)
			  : "r" (x2), "r" (x3), "r" (x4), "r" (x5), "r" (x6), "r" (x16)
			  : "memory", "cc");
	*err = failed ? (int)x0 : 0;
	return failed ? -1 : (int)x0;
}

static void probe_kq64(void)
{
	int kq = kqueue(), i;
	struct { const char *name; unsigned fflags; int64_t data; uint64_t leeway; int rel; } v[] = {
		{ "ms interval 100", 0, 100, 0, 0 },
		{ "MACHTIME rel 1e6", NOTE_MACHTIME, 1000000, 0, 0 },
		{ "MACHTIME|ABS now+10ms", NOTE_MACHTIME | NOTE_ABSOLUTE, 0, 0, 1 },
		{ "MACHTIME|ABS|LEEWAY", NOTE_MACHTIME | NOTE_ABSOLUTE | NOTE_LEEWAY, 0, 500000, 1 },
	};
	char b[200];

	mach_timebase_info(&tb);
	snprintf(b, sizeof b, "kq=%d timebase %u/%u abs=%llu\n", kq, tb.numer, tb.denom, mach_absolute_time());
	say(b);
	for (i = 0; i < 4; i++) {
		struct kevent64_s ev, out;
		struct timespec ts = { 0, 300000000 };
		int err = 0, err2 = 0, r, n;

		memset(&ev, 0, sizeof ev);
		ev.ident = 10 + i;
		ev.filter = EVFILT_TIMER;
		ev.flags = EV_ADD | EV_ENABLE | (i == 0 ? EV_ONESHOT : 0);
		ev.fflags = v[i].fflags;
		ev.data = v[i].rel ? (int64_t)(mach_absolute_time() + 10 * 1000000ULL * tb.denom / tb.numer) : v[i].data;
		ev.ext[1] = v[i].leeway;
		r = raw_kevent64(kq, &ev, 1, NULL, 0, NULL, &err);
		n = raw_kevent64(kq, NULL, 0, &out, 1, &ts, &err2);
		snprintf(b, sizeof b, "probe %-24s ret=%d errno=%d; wait n=%d errno=%d\n", v[i].name, r, err, n, err2);
		say(b);
	}
	/* flavour mixing: kevent() on a kevent64 kqueue */
	{
		struct kevent o;
		struct timespec ts = { 0, 1000000 };
		int n = kevent(kq, NULL, 0, &o, 1, &ts);

		snprintf(b, sizeof b, "probe legacy kevent() on the same kqueue: n=%d errno=%d\n", n, n < 0 ? errno : 0);
		say(b);
	}
}

int main(int argc, char **argv)
{
	int p[2], i;
	char msg[300];
	WMHandlerID (*addLeeway)(int, int, WMCallback *, void *);

	mach_timebase_info(&tb);
	if (argc == 2 && strcmp(argv[1], "kq64") == 0) {
		probe_kq64();
		return 0;
	}
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

		/* 4: same 1 s window twice, without and with a -60 s step in the middle */
		{
			int ref = 0, k;

			for (k = 0; k < 2; k++) {
				WMHandlerID pid;

				nfires = 0;
				step_secs = -60;
				t0 = mono_ms();
				pid = WMAddPersistentTimerHandler(100, cb_count, NULL);
				if (k)
					WMAddTimerHandler(500, cb_step, NULL);
				while (mono_ms() - t0 < 1000) {
					W_CheckTimerHandlers();
					W_HandleInputEvents(1, -1);
				}
				WMDeleteTimerHandler(pid);
				wall_restore();
				if (k == 0)
					ref = nfires;
				else {
					snprintf(msg, sizeof msg, "(%d fires in 1000 ms with the step, %d without)", nfires, ref);
					result("4 persistent timer over a -60 s step", nfires >= 3 && nfires >= ref - 1 && nfires <= ref + 1, msg);
				}
			}
		}
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
