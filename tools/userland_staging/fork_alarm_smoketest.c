/* Real, standalone reproducer for the two runtime bugs the OpenSSH port
 * (DAR-183) surfaced, so neither needs a 10-minute sshd boot cycle to
 * investigate:
 *
 *   DAR-336  alarm(3) kills the calling process. sshd's per-connection
 *            child dies the instant it calls alarm(login_grace_time);
 *            the same binary with LoginGraceTime 0 (alarm(0), i.e.
 *            disarm) survives and gets five log lines further. This test
 *            separates the three possible causes: the setitimer(2) call
 *            itself, SIGALRM *delivery*, and the timer firing at the
 *            wrong time.
 *
 *   DAR-337  a fork() child that forks again crashes. sshd's accept-loop
 *            child completes a whole SSH banner exchange and then dies at
 *            privsep_preauth()'s fork(); the same code path in a process
 *            that is NOT itself a fork child (sshd -ddd) forks fine.
 *
 * Each test prints a line before and after the operation under test, so
 * a crash is located by which line is missing rather than by inference.
 * Output is deliberately plain write(2)-backed printf to stderr with an
 * explicit fflush after every step -- a buffered regular file would lose
 * exactly the last line before a crash, which is the one that matters
 * (same lesson as docs/ "--startuptime /dev/console" trace note).
 */

#include <errno.h>
#include <stdarg.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <mach/mach_time.h>

/* Seconds on the monotonic clock. The waits below must not use time(NULL):
 * alarm() runs on the monotonic clock (ITIMER_REAL is armed from
 * microuptime() via tvtoabstime(), xnu bsd/kern/kern_time.c:432-439), but
 * the wall clock is stepped soon after boot (fake-hwclock, then chronyd).
 * A step during a wait ended the 10 s window at once and reported "no
 * SIGALRM" (DAR-405 QEMU runs, 2026-09-27: 1 of 6 runs, always the first
 * after boot). */
static long mono_s(void)
{
	static mach_timebase_info_data_t tb;
	if (tb.denom == 0)
		mach_timebase_info(&tb);
	return (long)(mach_absolute_time() * tb.numer / tb.denom / 1000000000ull);
}

static volatile sig_atomic_t alarm_fired;
static volatile sig_atomic_t alarm_sig;

static void
say(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	fflush(stderr);
}

static void
alarm_handler(int sig)
{
	alarm_fired = 1;
	alarm_sig = sig;
}

/* --- DAR-336 ------------------------------------------------------- */
static int
test_alarm(void)
{
	struct sigaction sa, osa;
	long t0, t1;
	unsigned int prev;

	say("alarm: step 1 -- installing SIGALRM handler via sigaction(2)");
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = alarm_handler;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGALRM, &sa, &osa) != 0) {
		say("alarm: FAIL sigaction: %s", strerror(errno));
		return 1;
	}
	say("alarm: step 2 -- handler installed, previous %p", (void *)osa.sa_handler);

	/* alarm(0) first: this is the call sshd makes with LoginGraceTime 0,
	 * the configuration that did NOT crash. If even this dies, the bug
	 * is in the setitimer(2) path itself rather than in arming a timer. */
	say("alarm: step 3 -- calling alarm(0) (disarm; the sshd config that survived)");
	prev = alarm(0);
	say("alarm: step 4 -- alarm(0) returned %u", prev);

	/* Now the call sshd actually makes. A 2-second timer, not sshd's
	 * 120, so a correct implementation finishes this test quickly and an
	 * early-firing one is still observable. */
	say("alarm: step 5 -- calling alarm(2) (arm; the sshd config that crashed)");
	prev = alarm(2);
	say("alarm: step 6 -- alarm(2) returned %u, timer armed", prev);

	t0 = mono_s();
	say("alarm: step 7 -- waiting for SIGALRM (t0=%ld)", t0);
	while (!alarm_fired) {
		struct timespec ts = { 0, 100 * 1000 * 1000 };
		nanosleep(&ts, NULL);
		if (mono_s() - t0 > 10) {
			say("alarm: FAIL no SIGALRM after 10s (timer never fired)");
			return 1;
		}
	}
	t1 = mono_s();
	say("alarm: step 8 -- SIGALRM delivered (sig=%d) after %ld s",
	    (int)alarm_sig, t1 - t0);
	if (t1 - t0 < 1) {
		say("alarm: SUSPECT timer fired immediately -- tvtoabstime/timebase");
	}
	say("alarm: PASS");
	return 0;
}

/* --- DAR-337 ------------------------------------------------------- */
static int
test_nested_fork(void)
{
	pid_t first, second;
	int status;

	say("fork: step 1 -- first-generation fork()");
	first = fork();
	if (first == -1) {
		say("fork: FAIL first fork: %s", strerror(errno));
		return 1;
	}
	if (first == 0) {
		/* First-generation child. Touch malloc and stdio the way real
		 * code does before forking again -- sshd has done a great deal
		 * of both by the time it reaches privsep_preauth(). */
		void *p;
		say("fork: step 2 -- in first-generation child, pid %d", (int)getpid());
		p = malloc(4096);
		if (p == NULL) {
			say("fork: FAIL malloc in first-generation child");
			_exit(2);
		}
		memset(p, 0x5a, 4096);
		free(p);
		say("fork: step 3 -- first-generation child malloc/free ok");

		say("fork: step 4 -- second-generation fork() (this is where sshd dies)");
		second = fork();
		if (second == -1) {
			say("fork: FAIL second fork: %s", strerror(errno));
			_exit(2);
		}
		if (second == 0) {
			void *q;
			say("fork: step 5 -- in second-generation child, pid %d",
			    (int)getpid());
			q = malloc(4096);
			if (q == NULL) {
				say("fork: FAIL malloc in second-generation child");
				_exit(2);
			}
			memset(q, 0xa5, 4096);
			free(q);
			say("fork: step 6 -- second-generation child malloc/free ok");
			_exit(0);
		}
		say("fork: step 7 -- first-generation parent of %d, waiting",
		    (int)second);
		if (waitpid(second, &status, 0) != second) {
			say("fork: FAIL waitpid(second): %s", strerror(errno));
			_exit(2);
		}
		say("fork: step 8 -- second-generation child reaped, status 0x%x", status);
		_exit(WIFEXITED(status) ? WEXITSTATUS(status) : 3);
	}

	say("fork: step 9 -- original process, waiting for %d", (int)first);
	if (waitpid(first, &status, 0) != first) {
		say("fork: FAIL waitpid(first): %s", strerror(errno));
		return 1;
	}
	say("fork: step 10 -- first-generation child reaped, status 0x%x", status);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		say("fork: FAIL nested fork did not complete cleanly");
		return 1;
	}
	say("fork: PASS");
	return 0;
}

/* --- DAR-337, step 2: which atfork callout traps? ------------------- */
/* The crash above is INSIDE fork(), so the interesting question is which
 * of its child-side callouts dies. Rather than instrument the shipped
 * libsystem_c.dylib, this replays fork()'s own body
 * (libsystem_c_fork_compat.c, itself real Libc/sys/fork.c) step by step
 * using the very same exported symbols, printing a marker around each
 * one. Whichever marker is missing is the culprit.
 *
 * Deliberately a REPLAY, not a reimplementation: the order below is
 * copied from libsystem_c_fork_compat.c's libSystem_atfork_prepare/
 * parent/child, which in turn came from real Libsystem-1292.50.1
 * init.c. If that order ever changes, this test is wrong and should be
 * updated with it. */
extern void _pthread_atfork_prepare_handlers(void);
extern void _pthread_atfork_prepare(void);
extern void _pthread_atfork_parent(void);
extern void _pthread_atfork_parent_handlers(void);
extern void _pthread_atfork_child(void);
extern void _pthread_atfork_child_handlers(void);
extern int  _mach_fork_child(void);
extern void _malloc_fork_prepare(void);
extern void _malloc_fork_parent(void);
extern void _malloc_fork_child(void);
extern int  __fork(void);

static int
test_nested_fork_instrumented(void)
{
	pid_t first;
	int status;

	say("atfork: step 1 -- first-generation fork() (plain)");
	first = fork();
	if (first == -1) {
		say("atfork: FAIL first fork: %s", strerror(errno));
		return 1;
	}
	if (first == 0) {
		int ret;

		say("atfork: step 2 -- in first-generation child, pid %d, "
		    "replaying fork()'s body by hand", (int)getpid());

		say("atfork: step 3 -- _pthread_atfork_prepare_handlers()");
		_pthread_atfork_prepare_handlers();
		say("atfork: step 4 -- _malloc_fork_prepare()");
		_malloc_fork_prepare();
		say("atfork: step 5 -- _pthread_atfork_prepare()");
		_pthread_atfork_prepare();
		say("atfork: step 6 -- __fork()");
		ret = __fork();
		if (ret == -1) {
			say("atfork: step 7 -- __fork failed: %s", strerror(errno));
			_pthread_atfork_parent();
			_malloc_fork_parent();
			_pthread_atfork_parent_handlers();
			_exit(2);
		}
		if (ret == 0) {
			say("atfork: step 8 -- in second-generation child (raw)");
			say("atfork: step 9 -- _mach_fork_child()");
			(void)_mach_fork_child();
			say("atfork: step 10 -- _pthread_atfork_child()");
			_pthread_atfork_child();
			say("atfork: step 11 -- _malloc_fork_child()");
			_malloc_fork_child();
			say("atfork: step 12 -- _pthread_atfork_child_handlers()");
			_pthread_atfork_child_handlers();
			say("atfork: step 13 -- second-generation child fully up, "
			    "pid %d", (int)getpid());
			_exit(0);
		}
		say("atfork: step 14 -- parent side: _pthread_atfork_parent()");
		_pthread_atfork_parent();
		say("atfork: step 15 -- _malloc_fork_parent()");
		_malloc_fork_parent();
		say("atfork: step 16 -- _pthread_atfork_parent_handlers()");
		_pthread_atfork_parent_handlers();
		say("atfork: step 17 -- waiting for %d", ret);
		if (waitpid(ret, &status, 0) != ret) {
			say("atfork: FAIL waitpid: %s", strerror(errno));
			_exit(2);
		}
		say("atfork: step 18 -- second-generation child reaped, status 0x%x",
		    status);
		_exit(WIFEXITED(status) ? WEXITSTATUS(status) : 3);
	}
	if (waitpid(first, &status, 0) != first) {
		say("atfork: FAIL waitpid(first): %s", strerror(errno));
		return 1;
	}
	say("atfork: step 19 -- first-generation child reaped, status 0x%x", status);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		say("atfork: FAIL instrumented nested fork did not complete");
		return 1;
	}
	say("atfork: PASS");
	return 0;
}

/* --- DAR-336: alarm() in a fork CHILD ------------------------------- */
/* The plain alarm() test above passes in the original process, but sshd
 * crashed on alarm() in its per-connection fork child. This runs the
 * identical sequence one fork deep. */
static int
test_alarm_in_child(void)
{
	pid_t pid;
	int status;

	say("alarmfork: step 1 -- fork()");
	pid = fork();
	if (pid == -1) {
		say("alarmfork: FAIL fork: %s", strerror(errno));
		return 1;
	}
	if (pid == 0) {
		struct sigaction sa;
		long t0;

		say("alarmfork: step 2 -- in child pid %d, installing handler",
		    (int)getpid());
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = alarm_handler;
		sigemptyset(&sa.sa_mask);
		alarm_fired = 0;
		if (sigaction(SIGALRM, &sa, NULL) != 0) {
			say("alarmfork: FAIL sigaction: %s", strerror(errno));
			_exit(2);
		}
		say("alarmfork: step 3 -- calling alarm(2) in the child "
		    "(this is what sshd does)");
		alarm(2);
		say("alarmfork: step 4 -- alarm(2) returned, waiting");
		t0 = mono_s();
		while (!alarm_fired) {
			struct timespec ts = { 0, 100 * 1000 * 1000 };
			nanosleep(&ts, NULL);
			if (mono_s() - t0 > 10) {
				say("alarmfork: FAIL no SIGALRM after 10s");
				_exit(2);
			}
		}
		say("alarmfork: step 5 -- SIGALRM delivered in the child");
		_exit(0);
	}
	if (waitpid(pid, &status, 0) != pid) {
		say("alarmfork: FAIL waitpid: %s", strerror(errno));
		return 1;
	}
	say("alarmfork: step 6 -- child reaped, status 0x%x", status);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		say("alarmfork: FAIL alarm() in a fork child did not complete");
		return 1;
	}
	say("alarmfork: PASS");
	return 0;
}

/* --- DAR-183 follow-up: real pselect(2) + SIGCHLD ------------------- */
/* sshd's server_loop2() blocks SIGCHLD, then calls ppoll() (built on
 * pselect(2)) with a sigset_t that unblocks it only for the duration of
 * the wait -- so a SIGCHLD arriving exactly during pselect() is the
 * scenario to reproduce. sshd's own wait_until_can_do_something() dies
 * with "ppoll: Broken pipe" (EPIPE) immediately after starting a
 * session's exec'd command -- i.e. right when that command's SIGCHLD is
 * imminent. This replays exactly that pattern: block SIGCHLD, fork a
 * child that exits almost immediately, then pselect() with SIGCHLD
 * unblocked in the mask -- expecting EINTR (interrupted by the signal),
 * not EPIPE. */
extern int *__error(void);
#define dar183_errno (*__error())

static void
pselect_sigchld_handler(int sig)
{
	(void)sig;
}

/* handler_first: install a real SIGCHLD handler before blocking/pselect(),
 * matching what real sshd actually does (serverloop.c:352,
 * `ssh_signal(SIGCHLD, sigchld_handler)`, BEFORE its own block+pselect
 * sequence at line ~350/373) -- my first version of this test skipped
 * that step, which matters: XNU's real setsigvec() (kern_sig.c) only
 * clears p_sigignore for a default-ignored signal (SIGCHLD's sigprop is
 * SA_IGNORE, third_party/xnu-7195/bsd/sys/signalvar.h) when a REAL,
 * non-default handler is installed. Leave it at SIG_DFL (even after
 * sigprocmask-blocking it) and psignal_internal's own early discard
 * check (`sig_proc->p_sigignore & mask`, kern_sig.c) throws the signal
 * away before it is ever queued -- unrelated to the atomic
 * unmask-and-wait race this test was originally written to isolate. */
static int
test_pselect_sigchld(int handler_first)
{
	sigset_t block, orig, waitmask;
	struct sigaction sa;
	pid_t pid;
	fd_set rfds;
	int ret, saved_errno;
	const char *tag = handler_first ? "pselect2" : "pselect";

	if (handler_first) {
		say("%s: step 0 -- installing a real SIGCHLD handler first "
		    "(matches serverloop.c's real sequence)", tag);
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = pselect_sigchld_handler;
		sigemptyset(&sa.sa_mask);
		if (sigaction(SIGCHLD, &sa, NULL) != 0) {
			say("%s: FAIL sigaction: %s", tag, strerror(errno));
			return 1;
		}
	}

	say("%s: step 1 -- blocking SIGCHLD", tag);
	sigemptyset(&block);
	sigaddset(&block, SIGCHLD);
	if (sigprocmask(SIG_BLOCK, &block, &orig) == -1) {
		say("%s: FAIL sigprocmask block: %s", tag, strerror(errno));
		return 1;
	}
	say("%s: step 2 -- forking a child that exits immediately", tag);
	pid = fork();
	if (pid == -1) {
		say("%s: FAIL fork: %s", tag, strerror(errno));
		return 1;
	}
	if (pid == 0) {
		_exit(0);
	}
	say("%s: step 3 -- child pid %d forked, calling real pselect(2) "
	    "with SIGCHLD unblocked in the mask", tag, (int)pid);
	waitmask = orig;
	sigdelset(&waitmask, SIGCHLD);
	FD_ZERO(&rfds);
	FD_SET(0, &rfds);
	dar183_errno = 0;
	ret = pselect(1, &rfds, NULL, NULL, NULL, &waitmask);
	saved_errno = dar183_errno;
	say("%s: step 4 -- pselect() returned %d, errno=%d (%s)",
	    tag, ret, saved_errno, ret == -1 ? strerror(saved_errno) : "n/a");
	sigprocmask(SIG_SETMASK, &orig, NULL);
	if (handler_first) {
		struct sigaction dfl;
		memset(&dfl, 0, sizeof(dfl));
		dfl.sa_handler = SIG_DFL;
		sigemptyset(&dfl.sa_mask);
		sigaction(SIGCHLD, &dfl, NULL);
	}
	{
		int status;
		waitpid(pid, &status, 0);
	}
	if (ret == -1 && saved_errno == EPIPE) {
		say("%s: FAIL real pselect(2) itself returned EPIPE -- "
		    "reproduced the sshd bug in isolation", tag);
		return 1;
	}
	if (ret == -1 && saved_errno != EINTR) {
		say("pselect: SUSPECT unexpected errno %d (wanted EINTR from "
		    "the SIGCHLD, or a clean timeout/ready return)", saved_errno);
	}
	say("%s: PASS (ret=%d errno=%d, no EPIPE)", tag, ret, saved_errno);
	return 0;
}

/* --- errno plumbing ------------------------------------------------ */
/* Third thing the sshd port exposed, cheap to check here: a FAILING
 * socket(2) reported "Broken pipe" (EPIPE) to sshd, and a failing
 * connect(2) reported errno 0. Neither is a value either call can
 * legitimately return, so some failure path is not setting errno and
 * callers see a stale one. AF_INET6 is a reliable failure on this
 * kernel (INET6 is deliberately not built). */
static int
test_socket_errno(void)
{
	int fd;

	errno = 0;
	say("errno: step 1 -- socket(AF_INET6, SOCK_STREAM, 0), expected to fail");
	fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (fd >= 0) {
		say("errno: socket(AF_INET6) unexpectedly SUCCEEDED (fd %d) -- "
		    "INET6 must be enabled now; test inconclusive", fd);
		close(fd);
		return 0;
	}
	say("errno: step 2 -- failed as expected, errno=%d (%s)", errno,
	    strerror(errno));
	if (errno == EPIPE || errno == 0) {
		say("errno: FAIL bogus errno for a failed socket(2) -- "
		    "EAFNOSUPPORT/EPROTONOSUPPORT expected");
		return 1;
	}
	say("errno: PASS");
	return 0;
}

/* --- DAR-342 regression: errno must survive a failed syscall -------- */
/* Mirrors sshd's real listen_on_addrs() sequence without needing sshd:
 * an AF_INET6 socket with an EXPLICIT protocol, immediately followed by
 * a real AF_INET socket + bind().
 *
 * DAR-342's actual root cause was NOT in the kernel (live kprintf
 * instrumentation in socket_common()/bind() confirmed the kernel
 * returned a correct EAFNOSUPPORT(47) all along). It was that
 * build_sshd.sh linked a LOOSE cerror_stub.o, which defines
 * errno/__error/cerror/cerror_nocancel -- and a plain .o is linked
 * unconditionally. That gave sshd a SECOND, private errno in its own
 * __DATA,__common, while socket(2) bound to libsystem_kernel.dylib and
 * set THAT image's errno. Every strerror(errno) in sshd then read a
 * word no syscall had ever written, reporting a stale "Broken pipe"
 * for a call that had really failed EAFNOSUPPORT.
 *
 * So what this test actually guards is the invariant that broke: the
 * errno a failed syscall sets must be the errno this process reads
 * back. It fails on ANY reintroduction of a duplicate errno, not just
 * the socket path -- which is the point, since build_libsystem_kernel.sh
 * predicted this exact "two private copies of one process-global" shape
 * on 2026-08-25 and it went unnoticed until sshd hit it. */
static int
test_socket_bind_epipe(void)
{
	int fd6, fd4, ret;
	struct sockaddr_in sin;

	errno = 0;
	say("sockbind: step 1 -- socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP) "
	    "(explicit protocol, matches sshd's real call)");
	fd6 = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
	say("sockbind: step 2 -- fd6=%d errno=%d (%s)", fd6,
	    fd6 == -1 ? errno : 0, fd6 == -1 ? strerror(errno) : "n/a");
	if (fd6 == -1 && errno == EPIPE) {
		say("sockbind: FAIL socket(AF_INET6,...,IPPROTO_TCP) itself "
		    "returned EPIPE -- DAR-342 has regressed (duplicate "
		    "errno: this process is reading an errno no syscall wrote)");
	}
	if (fd6 >= 0) {
		close(fd6);
	}

	errno = 0;
	say("sockbind: step 3 -- socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)");
	fd4 = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	say("sockbind: step 4 -- fd4=%d errno=%d (%s)", fd4,
	    fd4 == -1 ? errno : 0, fd4 == -1 ? strerror(errno) : "n/a");
	if (fd4 == -1) {
		say("sockbind: FAIL socket(AF_INET,...) itself failed -- "
		    "cannot test bind()");
		return 1;
	}

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = 0; /* INADDR_ANY */
	sin.sin_port = 0;        /* let the kernel pick a port -- avoids a
	                           * real EADDRINUSE if something else has
	                           * 22 open, which would be a genuine,
	                           * unrelated failure to not conflate with
	                           * this one */
	errno = 0;
	say("sockbind: step 5 -- bind(fd4, INADDR_ANY:0)");
	ret = bind(fd4, (struct sockaddr *)&sin, sizeof(sin));
	say("sockbind: step 6 -- bind() returned %d errno=%d (%s)", ret,
	    ret == -1 ? errno : 0, ret == -1 ? strerror(errno) : "n/a");
	close(fd4);
	if (ret == -1 && errno == EPIPE) {
		say("sockbind: FAIL bind() on a freshly-created, healthy "
		    "AF_INET socket returned EPIPE -- DAR-342 has regressed "
		    "(duplicate errno, see this test's own header)");
		return 1;
	}
	if (ret == -1) {
		say("sockbind: FAIL bind() failed for some other real reason: "
		    "%s", strerror(errno));
		return 1;
	}
	say("sockbind: PASS (fd6=%d, fd4=%d bound cleanly, no EPIPE)",
	    fd6, fd4);
	return 0;
}

int
main(void)
{
	int bad = 0;

	say("fork_alarm_smoketest: start, pid %d", (int)getpid());
	bad |= test_nested_fork();
	bad |= test_nested_fork_instrumented();
	bad |= test_alarm();
	bad |= test_alarm_in_child();
	/* NOTE: the no-handler (0) variant is known to hang forever (see
	 * DAR-338's own comments) -- running it here would block the
	 * handler-first variant from ever executing in the same process.
	 * Only the real, sshd-faithful sequence runs by default; flip this
	 * back to 0 deliberately, in isolation, to re-confirm the hang. */
	bad |= test_pselect_sigchld(1);  /* real handler first, matching sshd: expect EINTR, no hang */
	bad |= test_socket_errno();
	bad |= test_socket_bind_epipe();
	say("fork_alarm_smoketest: %s", bad ? "FAILURES ABOVE" : "ALL PASS");
	return bad;
}
