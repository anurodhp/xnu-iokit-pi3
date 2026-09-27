/*
 * DAR-417: does poll() on a kqueue fd report readiness correctly?
 *
 * Found in DAR-391. xf86-input-iohid's first version gave Xorg a kqueue fd
 * holding an EVFILT_MACHPORT knote on a port set. Xorg's ospoll uses
 * poll(), and read_input then ran about 400 times a second while kevent()
 * returned nothing and no Mach message was queued.
 *
 * Result (2026-09-27, QEMU): this is xnu's own behaviour, not a port bug.
 * bsd/kern/kern_event.c, sys_generic.c, osfmk/ipc/ipc_pset.c,
 * ipc_mqueue.c and osfmk/kern/waitq.c are unchanged from xnu-7195.81.3 in
 * this fork (git diff 528b90ac8..HEAD is empty for all five).
 * - An EVFILT_MACHPORT knote on a port SET is stay-active (osfmk/ipc/
 *   ipc_pset.c:788-792), so it stays on the kqueue's queue.
 * - poll() on the kqueue fd goes through filt_kqueue(), which only checks
 *   kq_count > 0 (bsd/kern/kern_event.c:940-944).
 * - select() goes through kqueue_select() (kern_event.c:4724), which
 *   reports ready as soon as a queued knote is KN_ACTIVE (:4782-4790), and
 *   knote_activate() sets that (:6667-6668).
 * So both report a port-set kqueue as readable when nothing is pending.
 * A knote on a single receive port is not stay-active, and poll() on it
 * behaves. Consequence: never hand a port-set kqueue fd to a poll()- or
 * select()-based event loop (xf86-input-iohid uses a thread and a pipe).
 *
 * Each case waits on a kqueue fd for readability with a 1 s timeout and
 * prints the wait's return value, revents, a zero-timeout kevent() count
 * and the elapsed time. "expect idle" means 0 after about 1000 ms; "expect
 * ready" means 1 at once; case 2p is the documented xnu behaviour.
 *   1.  poll, empty kqueue                                expect idle
 *   2p. poll, EVFILT_MACHPORT on an empty port set        expect ready (xnu)
 *   2s. select, same kqueue                               expect ready (xnu)
 *   3.  poll, same, one message queued                    expect ready
 *   4s. select, same, after the message is received       expect ready (xnu)
 *   5.  poll, EVFILT_MACHPORT on one empty receive port   expect idle
 *   6.  poll, EVFILT_READ on an empty pipe                expect idle
 *
 * Usage: kqueue_poll_smoketest.   Exit status 0 means every case matched.
 */
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/event.h>
#include <sys/select.h>
#include <mach/mach.h>
#include <mach/mach_time.h>

static long
elapsed_ms(uint64_t t0)
{
	static mach_timebase_info_data_t tb;
	if (tb.denom == 0)
		mach_timebase_info(&tb);
	return (long)((mach_absolute_time() - t0) * tb.numer / tb.denom / 1000000ull);
}

static int
check_how(const char *name, int kq, int want_ready, int use_select)
{
	struct pollfd pfd = { .fd = kq, .events = POLLIN };
	struct kevent ev;
	struct timespec zero = { 0, 0 };
	uint64_t t0 = mach_absolute_time();
	int r;
	if (use_select) {
		fd_set rd;
		struct timeval tv = { 1, 0 };
		FD_ZERO(&rd);
		FD_SET(kq, &rd);
		r = select(kq + 1, &rd, NULL, NULL, &tv);
		pfd.revents = (r > 0 && FD_ISSET(kq, &rd)) ? POLLIN : 0;
	} else {
		r = poll(&pfd, 1, 1000);
	}
	long ms = elapsed_ms(t0);
	int saved = errno;
	int nk = kevent(kq, NULL, 0, &ev, 1, &zero);
	int ok = want_ready ? (r == 1 && (pfd.revents & POLLIN) && nk == 1)
	                    : (r == 0 && nk == 0 && ms >= 900);

	printf("kqueue_poll_smoketest: %-38s poll=%d revents=0x%x errno=%d kevent=%d %ld ms -> %s\n",
	       name, r, pfd.revents, r < 0 ? saved : 0, nk, ms, ok ? "ok" : "WRONG");
	return ok ? 0 : 1;
}

#define check(n, k, w) check_how(n, k, w, 0)

int
main(void)
{
	int failures = 0;
	int kq;
	struct kevent ev;
	mach_port_t set, port;
	int p[2];

	kq = kqueue();
	failures += check("1 poll, empty kqueue", kq, 0);
	close(kq);

	kq = kqueue();
	mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_PORT_SET, &set);
	mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &port);
	mach_port_insert_right(mach_task_self(), port, port, MACH_MSG_TYPE_MAKE_SEND);
	mach_port_insert_member(mach_task_self(), port, set);
	EV_SET(&ev, set, EVFILT_MACHPORT, EV_ADD | EV_ENABLE, 0, 0, NULL);
	if (kevent(kq, &ev, 1, NULL, 0, NULL) < 0)
		printf("kqueue_poll_smoketest: EVFILT_MACHPORT add failed: %s\n", strerror(errno));
	/* xnu: stay-active port-set knote, so poll() always says ready. */
	{
		struct pollfd pfd = { .fd = kq, .events = POLLIN };
		int r = poll(&pfd, 1, 1000);
		printf("kqueue_poll_smoketest: %-38s poll=%d revents=0x%x -> %s\n",
		       "2p poll, empty port set (xnu: ready)", r, pfd.revents,
		       r == 1 ? "ok (as xnu)" : "CHANGED");
		failures += r == 1 ? 0 : 1;
	}
	{
		fd_set rd;
		struct timeval tv = { 1, 0 };
		FD_ZERO(&rd);
		FD_SET(kq, &rd);
		int r = select(kq + 1, &rd, NULL, NULL, &tv);
		printf("kqueue_poll_smoketest: %-38s select=%d -> %s\n",
		       "2s select, empty port set (xnu: ready)", r, r == 1 ? "ok (as xnu)" : "CHANGED");
		failures += r == 1 ? 0 : 1;
	}

	{
		mach_msg_header_t h = { 0 };
		h.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
		h.msgh_size = sizeof(h);
		h.msgh_remote_port = port;
		mach_msg(&h, MACH_SEND_MSG, sizeof(h), 0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
	}
	failures += check("3 poll, one message queued", kq, 1);
	{
		struct { mach_msg_header_t h; mach_msg_trailer_t t; } m;
		mach_msg(&m.h, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof(m), set, 0, MACH_PORT_NULL);
	}
	{
		fd_set rd;
		struct timeval tv = { 1, 0 };
		FD_ZERO(&rd);
		FD_SET(kq, &rd);
		int r = select(kq + 1, &rd, NULL, NULL, &tv);
		printf("kqueue_poll_smoketest: %-38s select=%d -> %s\n",
		       "4s select, received (xnu: ready)", r, r == 1 ? "ok (as xnu)" : "CHANGED");
		failures += r == 1 ? 0 : 1;
	}
	close(kq);

	kq = kqueue();
	{
		mach_port_t one;
		mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &one);
		EV_SET(&ev, one, EVFILT_MACHPORT, EV_ADD | EV_ENABLE, 0, 0, NULL);
		if (kevent(kq, &ev, 1, NULL, 0, NULL) < 0)
			printf("kqueue_poll_smoketest: single-port add failed: %s\n", strerror(errno));
	}
	failures += check("5 poll, one empty receive port", kq, 0);
	close(kq);

	kq = kqueue();
	pipe(p);
	EV_SET(&ev, p[0], EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
	kevent(kq, &ev, 1, NULL, 0, NULL);
	failures += check("6 poll, EVFILT_READ on empty pipe", kq, 0);
	close(kq);

	printf("kqueue_poll_smoketest: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
