/* Real EVFILT_READ / EVFILT_PROC / select()-on-a-kqueue-fd smoketest --
 * the "cheapest decisive test" DAR-200 asks for, isolating "does this
 * port's kernel deliver kqueue events at all" from "is something in
 * launchd's own machinery broken".
 *
 * kqueue_smoketest.c (this file's older sibling, same
 * build_kqueue_smoketest.sh) already proves real kqueue()/kevent() work
 * end to end -- but deliberately only for EVFILT_TIMER, and deliberately
 * never calls select() at all ("keeping this smoketest's own dependency
 * surface to just kqueue()+kevent()"). DAR-200's failure needs exactly
 * the three things that test skipped:
 *
 *   A. select(2) on an AF_UNIX socket fd            -- control
 *   B. select(2) on a *kqueue* fd                   -- kqueue_select(),
 *                                                      bsd/kern/kern_event.c
 *   C. EVFILT_READ on an AF_UNIX socket             -- soo_kqfilter()/
 *                                                      filt_soread(),
 *                                                      bsd/kern/uipc_socket.c
 *   D. EVFILT_PROC/NOTE_EXIT on a real forked child -- filt_proc(),
 *                                                      bsd/kern/kern_event.c
 *
 * B and C together are precisely launchd's real check-in path
 * (ipc_open() registers EVFILT_READ on the parent's socketpair end;
 * kqueue_demand_loop() select()s on mainkq), and D is its real
 * reap/respawn path. Every step prints its own real return value with
 * raw write(2), never printf() -- see kqueue_smoketest.c's own header
 * comment for the real, separate, still-undiagnosed printf() crash in
 * this specific minimal link recipe.
 *
 * The child in step D is released by a second socketpair rather than by
 * sleeping, so the EVFILT_PROC registration provably happens while the
 * child is still alive -- no race, no timing assumption.
 */
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/select.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

/* Output fd. Defaults to stderr, but main() re-points it at /dev/console
 * when that can be opened, so this test produces real, visible output in
 * BOTH the ways it gets run: as a launchd job (stderr already wired to
 * /dev/console by the plist) and as PID 1 itself (where nothing has set
 * up fds 0/1/2 at all -- see build_kqueue_socket_smoketest.sh and
 * inject_into_sd_image.sh's KQUEUE_SOCKET_SMOKETEST_AS_PID1 knob for why
 * that second mode exists). The kernel mounts devfs itself in bsd_init()
 * (bsd/kern/bsd_init.c's devfs_kernel_mount("/dev"), before
 * load_init_program), so /dev/console is real and open-able even for
 * PID 1. */
static int g_out = 2;

/* Raw write(2), not printf() -- see header comment. */
static void diag(const char *s)
{
	write(g_out, s, strlen(s));
}

static void diag_num(const char *label, long v)
{
	char buf[32];
	int i = (int)sizeof(buf);
	int neg = 0;
	unsigned long u;

	write(g_out, label, strlen(label));
	if (v < 0) {
		neg = 1;
		u = (unsigned long)(-v);
	} else {
		u = (unsigned long)v;
	}
	buf[--i] = '\n';
	if (u == 0) {
		buf[--i] = '0';
	}
	while (u) {
		buf[--i] = (char)('0' + (u % 10));
		u /= 10;
	}
	if (neg) {
		buf[--i] = '-';
	}
	write(g_out, buf + i, (size_t)(sizeof(buf) - (size_t)i));
}

/* #135 AUE_SOCKETPAIR. Not in build_simple_cmds.sh's SYS_STUBS_NEEDED
 * list and not in any archive this minimal link pulls in, so written
 * here with the same real carry-flag ABI every other raw syscall in this
 * project uses (see libc_gen_syscalls.c's own header comment). */
extern long cerror_nocancel(int err);

static int t_socketpair(int domain, int type, int protocol, int sv[2])
{
	register long x0 asm("x0") = domain;
	register long x1 asm("x1") = type;
	register long x2 asm("x2") = protocol;
	register long x3 asm("x3") = (long)sv;
	register long x16 asm("x16") = 135;
	long is_error;

	asm volatile("svc #0x80\n"
	             "cset %1, cs"
	             : "+r"(x0), "=r"(is_error)
	             : "r"(x1), "r"(x2), "r"(x3), "r"(x16)
	             : "memory", "cc");
	if (is_error) {
		return (int)cerror_nocancel((int)x0);
	}
	return (int)x0;
}

/* #2 AUE_FORK -- real arm64 XNU ABI quirk: the raw trap returns the
 * child's pid in x0 to BOTH sides, x1 is the only real discriminator
 * (0=parent, 1=child). Same body as login_syscalls.c's own fork(),
 * renamed so it never collides with a real archive-provided fork(). */
static pid_t t_fork(void)
{
	register long x0 asm("x0") = 0;
	register long x1 asm("x1") = 0;
	register long x16 asm("x16") = 2;
	long is_error;

	asm volatile("svc #0x80\n"
	             "cset %2, cs"
	             : "+r"(x0), "+r"(x1), "=r"(is_error)
	             : "r"(x16)
	             : "memory", "cc");
	if (is_error) {
		return (pid_t)cerror_nocancel((int)x0);
	}
	return (pid_t)(x1 ? 0 : x0);
}

/* #5 AUE_OPEN_RWTC / #20 AUE_GETPID -- both real, both needed only for
 * the PID-1 mode described at g_out's declaration. Raw here for the same
 * reason t_socketpair() is: this deliberately minimal link pulls in no
 * archive that already provides them. */
static int t_open(const char *path, int flags)
{
	register long x0 asm("x0") = (long)path;
	register long x1 asm("x1") = flags;
	register long x2 asm("x2") = 0;
	register long x16 asm("x16") = 5;
	long is_error;

	asm volatile("svc #0x80\n"
	             "cset %1, cs"
	             : "+r"(x0), "=r"(is_error)
	             : "r"(x1), "r"(x2), "r"(x16)
	             : "memory", "cc");
	if (is_error) {
		return -1;
	}
	return (int)x0;
}

static pid_t t_getpid(void)
{
	register long x0 asm("x0") = 0;
	register long x16 asm("x16") = 20;

	asm volatile("svc #0x80" : "+r"(x0) : "r"(x16) : "memory", "cc");
	return (pid_t)x0;
}

/* #1 AUE_EXIT, never returns. */
static void t_exit(int code)
{
	register long x0 asm("x0") = code;
	register long x16 asm("x16") = 1;

	asm volatile("svc #0x80" : "+r"(x0) : "r"(x16) : "memory");
	for (;;) {
	}
}

/* Real _select() raw syscall (#93, NO_SYSCALL_STUB) already built into
 * libc_build/libc_gen.a -- see libc_gen_syscalls.c. Called directly
 * rather than through a public select() wrapper so this test depends on
 * nothing but the raw trap. */
extern int _select(int nfds, void *readfds, void *writefds, void *exceptfds,
    void *timeout);

#define AF_UNIX_T     1
#define SOCK_STREAM_T 1

int main(void)
{
	int sv[2], rel[2], kq, n, r;
	struct kevent chg, evt;
	struct timespec zero_ts = { 0, 0 };
	struct timeval tv;
	fd_set rfds;
	pid_t child;
	char c;
	int failures = 0;

	/* O_WRONLY. Failure is fine and non-fatal -- keep stderr. */
	{
		int cfd = t_open("/dev/console", 0x0001);
		if (cfd >= 0) {
			g_out = cfd;
		}
	}

	diag("KQS: start\n");
	diag_num("KQS: getpid=", (long)t_getpid());

	if (t_socketpair(AF_UNIX_T, SOCK_STREAM_T, 0, sv) < 0) {
		diag("KQS: FAIL socketpair\n");
		return 1;
	}
	diag_num("KQS: socketpair sv0=", sv[0]);
	diag_num("KQS: socketpair sv1=", sv[1]);

	if ((kq = kqueue()) < 0) {
		diag("KQS: FAIL kqueue\n");
		return 1;
	}
	diag_num("KQS: kqueue fd=", kq);

	/* Register EVFILT_READ on the read end -- exactly what launchd's
	 * ipc_open() does for the check-in socketpair. */
	EV_SET(&chg, (uintptr_t)sv[0], EVFILT_READ, EV_ADD, 0, 0, (void *)0x1234);
	n = kevent(kq, &chg, 1, NULL, 0, &zero_ts);
	diag_num("KQS: kevent(EV_ADD EVFILT_READ) rc=", n);
	if (n < 0) {
		diag_num("KQS: FAIL EV_ADD errno=", errno);
		failures++;
	}

	/* Client side writes, exactly as syslogd's check-in sendmsg() does. */
	r = (int)write(sv[1], "hello", 5);
	diag_num("KQS: write(sv1) rc=", r);

	/* --- A. control: select() on the socket fd itself --- */
	FD_ZERO(&rfds);
	FD_SET(sv[0], &rfds);
	tv.tv_sec = 3;
	tv.tv_usec = 0;
	r = _select(sv[0] + 1, &rfds, NULL, NULL, &tv);
	diag_num("KQS: A select(socket fd) rc=", r);
	if (r != 1) {
		diag("KQS: A FAIL -- select() does not work on AF_UNIX sockets\n");
		failures++;
	}

	/* --- B. the DAR-200 candidate #1: select() on a kqueue fd --- */
	FD_ZERO(&rfds);
	FD_SET(kq, &rfds);
	tv.tv_sec = 3;
	tv.tv_usec = 0;
	r = _select(kq + 1, &rfds, NULL, NULL, &tv);
	diag_num("KQS: B select(kqueue fd) rc=", r);
	if (r != 1) {
		diag("KQS: B FAIL -- select() on a kqueue fd never reports readable\n");
		diag_num("KQS: B errno=", errno);
		failures++;
	}

	/* --- C. EVFILT_READ actually fires --- */
	memset(&evt, 0, sizeof(evt));
	n = kevent(kq, NULL, 0, &evt, 1, &zero_ts);
	diag_num("KQS: C kevent(drain) rc=", n);
	if (n != 1) {
		diag("KQS: C FAIL -- EVFILT_READ on AF_UNIX socket never fired\n");
		failures++;
	} else {
		diag_num("KQS: C   ident=", (long)evt.ident);
		diag_num("KQS: C   filter=", (long)evt.filter);
		diag_num("KQS: C   data=", (long)evt.data);
		diag_num("KQS: C   udata=", (long)evt.udata);
		if (evt.ident != (uintptr_t)sv[0] || evt.filter != EVFILT_READ) {
			diag("KQS: C FAIL -- wrong ident/filter\n");
			failures++;
		}
	}

	/* Retire the EVFILT_READ knote before step D. EVFILT_READ is
	 * level-triggered and those 5 bytes were never read, so leaving it
	 * registered makes the next kevent() drain return the socket event
	 * again instead of the process event -- which is exactly what the
	 * first real run of this test did (qemu/dar200_kqueue_pid1_test6.log:
	 * "D kevent(wait NOTE_EXIT) rc=1 / ident=1 / filter=-1", i.e. the
	 * EVFILT_READ event a second time, making step D's result
	 * meaningless rather than merely wrong). */
	EV_SET(&chg, (uintptr_t)sv[0], EVFILT_READ, EV_DELETE, 0, 0, NULL);
	n = kevent(kq, &chg, 1, NULL, 0, &zero_ts);
	diag_num("KQS: D kevent(EV_DELETE EVFILT_READ) rc=", n);

	/* --- D. EVFILT_PROC / NOTE_EXIT on a real child --- */
	if (t_socketpair(AF_UNIX_T, SOCK_STREAM_T, 0, rel) < 0) {
		diag("KQS: D FAIL release socketpair\n");
		return failures + 1;
	}
	child = t_fork();
	if (child == 0) {
		/* Block until the parent has registered EVFILT_PROC. */
		(void)read(rel[1], &c, 1);
		t_exit(7);
	}
	if (child < 0) {
		diag("KQS: D FAIL fork\n");
		failures++;
	} else {
		diag_num("KQS: D child pid=", child);
		EV_SET(&chg, (uintptr_t)child, EVFILT_PROC, EV_ADD, NOTE_EXIT, 0,
		    (void *)0x5678);
		n = kevent(kq, &chg, 1, NULL, 0, &zero_ts);
		diag_num("KQS: D kevent(EV_ADD EVFILT_PROC) rc=", n);
		if (n < 0) {
			diag_num("KQS: D FAIL EV_ADD errno=", errno);
			failures++;
		}
		/* Release the child; it exits immediately. */
		(void)write(rel[0], "g", 1);

		memset(&evt, 0, sizeof(evt));
		zero_ts.tv_sec = 5;
		n = kevent(kq, NULL, 0, &evt, 1, &zero_ts);
		diag_num("KQS: D kevent(wait NOTE_EXIT) rc=", n);
		if (n != 1) {
			diag("KQS: D FAIL -- EVFILT_PROC/NOTE_EXIT never fired\n");
			failures++;
		} else {
			diag_num("KQS: D   ident=", (long)evt.ident);
			diag_num("KQS: D   filter=", (long)evt.filter);
			diag_num("KQS: D   fflags=", (long)evt.fflags);
			if (evt.ident != (uintptr_t)child ||
			    evt.filter != EVFILT_PROC ||
			    (evt.fflags & NOTE_EXIT) == 0) {
				diag("KQS: D FAIL -- not the child's NOTE_EXIT event\n");
				failures++;
			}
		}
	}

	close(kq);
	close(sv[0]);
	close(sv[1]);
	close(rel[0]);
	close(rel[1]);

	if (failures) {
		diag_num("KQS: DONE failures=", failures);
	} else {
		diag("KQS: PASS all (select-on-kqueue, EVFILT_READ/unix, EVFILT_PROC)\n");
	}

	/* PID-1 mode: returning here would exit PID 1, which XNU treats as a
	 * fatal condition -- park instead so the console log stays readable
	 * and the run ends on boot_test.py's own timeout. */
	if (t_getpid() == 1) {
		diag("KQS: running as PID 1 -- parking (no init exit)\n");
		for (;;) {
			struct timeval sl = { 3600, 0 };
			(void)_select(0, NULL, NULL, NULL, &sl);
		}
	}
	return failures ? 1 : 0;
}
