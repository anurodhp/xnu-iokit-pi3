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
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

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
	time_t t0, t1;
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

	t0 = time(NULL);
	say("alarm: step 7 -- waiting for SIGALRM (t0=%ld)", (long)t0);
	while (!alarm_fired) {
		struct timespec ts = { 0, 100 * 1000 * 1000 };
		nanosleep(&ts, NULL);
		if (time(NULL) - t0 > 10) {
			say("alarm: FAIL no SIGALRM after 10s (timer never fired)");
			return 1;
		}
	}
	t1 = time(NULL);
	say("alarm: step 8 -- SIGALRM delivered (sig=%d) after %ld s",
	    (int)alarm_sig, (long)(t1 - t0));
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
		time_t t0;

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
		t0 = time(NULL);
		while (!alarm_fired) {
			struct timespec ts = { 0, 100 * 1000 * 1000 };
			nanosleep(&ts, NULL);
			if (time(NULL) - t0 > 10) {
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

int
main(void)
{
	int bad = 0;

	say("fork_alarm_smoketest: start, pid %d", (int)getpid());
	bad |= test_nested_fork();
	bad |= test_nested_fork_instrumented();
	bad |= test_alarm();
	bad |= test_alarm_in_child();
	bad |= test_socket_errno();
	say("fork_alarm_smoketest: %s", bad ? "FAILURES ABOVE" : "ALL PASS");
	return bad;
}
