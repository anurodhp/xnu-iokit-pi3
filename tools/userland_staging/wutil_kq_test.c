/*
 * wutil_kq_test (DAR-431): functional test of libWUtil's input wait
 * (WINGs/handlers.c in anurodhp/wmaker), through the public WUtil API plus
 * the internal W_KQueue* filter hook. Run it twice, once per backend:
 *   wutil_kq_test                        kqueue (default)
 *   WM_EVENT_BACKEND=select wutil_kq_test  select() fallback
 * Both must pass every case except "extension filter", which is kqueue-only
 * (W_KQueueAddFilter returns NULL under select and the case is skipped).
 *
 *  1 pipe read handler fires from a timer-driven write
 *  2 level-triggered: an undrained pipe fires on every WHandleEvents()
 *  3 write handler on a socketpair fires at once
 *  4 a handler deleted by an earlier callback in the same batch never fires
 *  5 EOF on a pipe is reported as readable
 *  6 AF_UNIX stream socket (the X connection's transport) read handler
 *  7 an fd closed without WMDeleteInputHandler neither spins nor blocks a
 *    pending timer (the wait ends at the timer, about 100 ms)
 *  8 timer precision: a 100 ms timer fires within 100..160 ms
 *  9 extension filter: EVFILT_TIMER through W_KQueueAddFilter fires once
 * 10 fork: the child's own WHandleEvents() sees fresh data, and the
 *    parent's still works afterwards (xnu does not copy a kqueue fd into
 *    a child, kern_event.c:3025)
 * 11 handler added and deleted from inside callbacks
 * Cases use W_HandleInputEvents(False) peeks, or WHandleEvents()'s sequence
 * without its final unconditional blocking wait (which would block forever
 * once the awaited event was consumed by the peek).
 * Exit status 0 means every case passed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/event.h>
#include <sys/time.h>

typedef void WMCallback(void *clientData);
typedef void WMInputProc(int fd, int mask, void *clientData);
typedef void *WMHandlerID;
extern WMHandlerID WMAddTimerHandler(int ms, WMCallback *cb, void *cd);
extern void WMDeleteTimerHandler(WMHandlerID id);
extern WMHandlerID WMAddInputHandler(int fd, int cond, WMInputProc *proc, void *cd);
extern void WMDeleteInputHandler(WMHandlerID id);
extern int W_HandleInputEvents(int waitForInput, int inputfd);
extern void W_CheckTimerHandlers(void);
typedef void W_KQueueProc(const struct kevent *ev, void *clientData);
extern void *W_KQueueAddFilter(const struct kevent *kev, W_KQueueProc *proc, void *cd);
extern void W_KQueueDeleteFilter(void *id);
enum { WIReadMask = 1, WIWriteMask = 2, WIExceptMask = 4 };

static int failures;

static void say(const char *s)
{
	write(1, s, strlen(s));
}

static void result(const char *name, int ok, const char *extra)
{
	char b[200];
	snprintf(b, sizeof b, "%-4s %s%s%s\n", ok ? "PASS" : "FAIL", name, extra ? "  " : "", extra ? extra : "");
	say(b);
	if (!ok)
		failures++;
}

static long now_ms(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000L + tv.tv_usec / 1000;
}

static int hits, lastmask, drain_fd = -1;

static void on_input(int fd, int mask, void *cd)
{
	hits++;
	lastmask = mask;
	if (cd && *(int *)cd) {
		char c[64];
		read(fd, c, sizeof c);
	}
}

static int flag;
static void set_flag(void *cd)
{
	flag++;
	if (cd)
		write(*(int *)cd, "x", 1);
}

/* One input peek, dispatching ready handlers (WHandleEvents()'s first half). */
static int peek(void)
{
	W_CheckTimerHandlers();
	return W_HandleInputEvents(0, -1);
}

/*
 * WHandleEvents() minus its unconditional final blocking wait, which would
 * block forever once the awaited event was consumed by the peek: timers,
 * peek, and only block if *cond is still false.
 */
static void step(int *cond)
{
	W_CheckTimerHandlers();
	if (W_HandleInputEvents(0, -1) || *cond)
		return;
	W_HandleInputEvents(1, -1);
	W_CheckTimerHandlers();
}

/* step() until *cond or timeout ms. */
static void pump_until(int *cond, int ms)
{
	long end = now_ms() + ms;
	while (!*cond && now_ms() < end)
		step(cond);
}

static int *del_victim_fd;
static WMHandlerID victim_id;
static int victim_fired;
static void killer_cb(int fd, int mask, void *cd)
{
	char c[8];
	read(fd, c, sizeof c);
	WMDeleteInputHandler(victim_id);
}
static void victim_cb(int fd, int mask, void *cd)
{
	victim_fired++;
}

static int cb11_fired;
static WMHandlerID id11b;
static int p11b[2];
static void cb11b(int fd, int mask, void *cd)
{
	char c[8];
	read(fd, c, sizeof c);
	cb11_fired += 10;
	WMDeleteInputHandler(id11b);
}
static void cb11a(int fd, int mask, void *cd)
{
	char c[8];
	read(fd, c, sizeof c);
	cb11_fired++;
	pipe(p11b);
	id11b = WMAddInputHandler(p11b[0], WIReadMask, cb11b, NULL);
	write(p11b[1], "y", 1);
}

static int ext_hits;
static void ext_cb(const struct kevent *ev, void *cd)
{
	if (ev->filter == EVFILT_TIMER)
		ext_hits++;
}

int main(void)
{
	int p[2], q[2], sv[2], i, nodrain = 0, yes = 1;
	WMHandlerID h, h2;
	long t0, dt;
	char c[64], msg[160];

	say(getenv("WM_EVENT_BACKEND") ? "backend: WM_EVENT_BACKEND set\n" : "backend: default\n");

	/* 1 */
	pipe(p);
	hits = 0;
	h = WMAddInputHandler(p[0], WIReadMask, on_input, &yes);
	WMAddTimerHandler(50, set_flag, &p[1]);
	t0 = now_ms();
	pump_until(&hits, 2000);
	dt = now_ms() - t0;
	snprintf(msg, sizeof msg, "(%ld ms, mask %d)", dt, lastmask);
	result("1 pipe read handler", hits == 1 && lastmask == WIReadMask && dt >= 40, msg);
	WMDeleteInputHandler(h);

	/* 2 */
	write(p[1], "abc", 3);
	h = WMAddInputHandler(p[0], WIReadMask, on_input, &nodrain);
	hits = 0;
	for (i = 0; i < 3; i++)
		peek();
	snprintf(msg, sizeof msg, "(%d hits in 3 peeks)", hits);
	result("2 level-triggered", hits == 3, msg);
	WMDeleteInputHandler(h);
	read(p[0], c, sizeof c);

	/* 3 */
	socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
	hits = 0;
	h = WMAddInputHandler(sv[0], WIWriteMask, on_input, &nodrain);
	peek();
	result("3 write handler", hits == 1 && lastmask == WIWriteMask, NULL);
	WMDeleteInputHandler(h);

	/* 4 */
	pipe(q);
	write(p[1], "k", 1);
	write(q[1], "v", 1);
	victim_fired = 0;
	h = WMAddInputHandler(p[0], WIReadMask, killer_cb, NULL);
	victim_id = WMAddInputHandler(q[0], WIReadMask, victim_cb, NULL);
	peek();
	peek();
	result("4 delete in callback", victim_fired == 0, NULL);
	WMDeleteInputHandler(h);
	close(q[0]);
	close(q[1]);

	/* 5 */
	pipe(q);
	close(q[1]);
	hits = 0;
	h = WMAddInputHandler(q[0], WIReadMask, on_input, &nodrain);
	peek();
	result("5 EOF readable", hits >= 1 && lastmask == WIReadMask, NULL);
	WMDeleteInputHandler(h);
	close(q[0]);

	/* 6 */
	{
		int ls, cs, as;
		struct sockaddr_un a;
		memset(&a, 0, sizeof a);
		a.sun_family = AF_UNIX;
		snprintf(a.sun_path, sizeof a.sun_path, "/tmp/wutil_kq_test.%d", getpid());
		a.sun_len = sizeof a;
		unlink(a.sun_path);
		ls = socket(AF_UNIX, SOCK_STREAM, 0);
		bind(ls, (struct sockaddr *)&a, sizeof a);
		listen(ls, 1);
		cs = socket(AF_UNIX, SOCK_STREAM, 0);
		connect(cs, (struct sockaddr *)&a, sizeof a);
		as = accept(ls, NULL, NULL);
		hits = 0;
		h = WMAddInputHandler(as, WIReadMask, on_input, &yes);
		WMAddTimerHandler(30, set_flag, &cs);
		pump_until(&hits, 2000);
		result("6 AF_UNIX read handler", hits == 1, NULL);
		WMDeleteInputHandler(h);
		close(as);
		close(cs);
		close(ls);
		unlink(a.sun_path);
	}

	/* 7 */
	pipe(q);
	h = WMAddInputHandler(q[0], WIReadMask, on_input, &nodrain);
	close(q[0]);
	close(q[1]);
	flag = 0;
	hits = 0;
	WMAddTimerHandler(100, set_flag, NULL);
	t0 = now_ms();
	for (i = 0; !flag && i < 20000; i++)
		step(&flag);
	dt = now_ms() - t0;
	snprintf(msg, sizeof msg, "(%ld ms, %d iterations)", dt, i);
	if (getenv("WM_EVENT_BACKEND") && !strcmp(getenv("WM_EVENT_BACKEND"), "select")) {
		/* legacy behaviour: select() returns EBADF at once, so this spins */
		say("INFO 7 closed fd under select (expected to spin) "); say(msg); say("\n");
	} else {
		result("7 closed fd, no spin", flag == 1 && i < 20 && dt >= 90 && dt < 400, msg);
	}
	WMDeleteInputHandler(h);

	/* 8 */
	flag = 0;
	WMAddTimerHandler(100, set_flag, NULL);
	t0 = now_ms();
	pump_until(&flag, 2000);
	dt = now_ms() - t0;
	snprintf(msg, sizeof msg, "(%ld ms)", dt);
	result("8 timer precision", flag == 1 && dt >= 95 && dt < 200, msg);

	/* 9 */
	{
		struct kevent kev;
		void *id;
		EV_SET(&kev, 77, EVFILT_TIMER, EV_ADD | EV_ONESHOT, 0, 40, NULL);
		id = W_KQueueAddFilter(&kev, ext_cb, NULL);
		if (!id) {
			say("SKIP 9 extension filter (select backend)\n");
		} else {
			t0 = now_ms();
			/* the filter alone keeps the wait alive: no fds, no timers */
			for (i = 0; !ext_hits && now_ms() - t0 < 2000; i++)
				step(&ext_hits);
			dt = now_ms() - t0;
			snprintf(msg, sizeof msg, "(%ld ms)", dt);
			result("9 extension filter", ext_hits == 1 && dt >= 30 && dt < 300, msg);
			W_KQueueDeleteFilter(id);
		}
	}

	/* 10 */
	hits = 0;
	h = WMAddInputHandler(p[0], WIReadMask, on_input, &yes);
	{
		pid_t pid = fork();
		if (pid == 0) {
			int st = 0;
			int sp[2];
			pipe(sp);
			WMDeleteInputHandler(h);
			h2 = WMAddInputHandler(sp[0], WIReadMask, on_input, &yes);
			hits = 0;
			write(sp[1], "c", 1);
			peek();
			st = hits == 1 ? 0 : 1;
			_exit(st);
		} else {
			int st = -1;
			waitpid(pid, &st, 0);
			snprintf(msg, sizeof msg, "(child status %d)", WEXITSTATUS(st));
			write(p[1], "z", 1);
			hits = 0;
			peek();
			result("10 fork", WIFEXITED(st) && WEXITSTATUS(st) == 0 && hits == 1, msg);
		}
	}
	WMDeleteInputHandler(h);

	/* 11 */
	pipe(q);
	cb11_fired = 0;
	h = WMAddInputHandler(q[0], WIReadMask, cb11a, NULL);
	write(q[1], "a", 1);
	for (i = 0; i < 5 && cb11_fired < 11; i++)
		peek();
	snprintf(msg, sizeof msg, "(fired %d)", cb11_fired);
	result("11 add/delete in callbacks", cb11_fired == 11, msg);
	WMDeleteInputHandler(h);

	say(failures ? "FAILED\n" : "ALL PASSED\n");
	return failures != 0;
}
