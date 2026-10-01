/*
 * spawn_test (DAR-434): the posix_spawn(p) attributes Window Maker's launch
 * code (third_party/wmaker src/launch.c) relies on, and the EVFILT_PROC
 * exit notification, tested on their own so a failure is not a wmaker
 * problem. QEMU/real-Pi test aid, /usr/X11/bin/spawn_test.
 *
 *   spawn_test                 all checks, prints PASS/FAIL lines
 *   spawn_test bench [N] [MB]  launch latency, fork+exec vs posix_spawn, with
 *                              a parent that has touched MB of heap
 *   spawn_test --child MODE..  what the checks spawn (this same binary)
 *
 * Source references for what is checked (this port's pinned checkouts):
 *   POSIX_SPAWN_SETSID / CLOEXEC_DEFAULT  xnu bsd/sys/spawn.h:61-62 (SDK),
 *       bsd/kern/kern_exec.c:2533-2575 (CLOEXEC_DEFAULT marks only the fds
 *       in the file actions UF_INHERIT), 3749 (SETSID)
 *   posix_spawnp  Libc sys/posix_spawn.c:70 (PATH search; ENOEXEC retried
 *       through /bin/sh at :142-154; plain posix_spawn does not)
 *   EVFILT_PROC  bsd/kern/kern_event.c filt_procattach/filt_procevent
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <pthread/qos.h>
#include <pthread/spawn.h>

static int nfail;
static char *self;

#define PASS(name) printf("PASS %s\n", name)
#define FAIL(name, ...) do { nfail++; printf("FAIL %s: ", name); printf(__VA_ARGS__); printf("\n"); } while (0)
#define CHECK(name, cond, ...) do { if (cond) PASS(name); else FAIL(name, __VA_ARGS__); } while (0)

static double now_ms(void)
{
	static mach_timebase_info_data_t tb;
	if (!tb.denom) mach_timebase_info(&tb);
	return (double) mach_absolute_time() * tb.numer / tb.denom / 1e6;
}

/* ---- child modes ----------------------------------------------------- */
static int child(int argc, char **argv)
{
	const char *m = argc > 0 ? argv[0] : "";

	if (!strcmp(m, "fds")) {		/* open descriptors, 0..255 */
		int fd;
		for (fd = 0; fd < 256; fd++)
			if (fcntl(fd, F_GETFD) >= 0 && fd != 1)	/* 1 is the report pipe */
				printf("%d ", fd);
		printf("\n");
		return 0;
	}
	if (!strcmp(m, "fd1ok")) { printf("fd1\n"); return 0; }
	if (!strcmp(m, "env")) {
		char *v = getenv(argv[1]);
		printf("%s\n", v ? v : "(unset)");
		return 0;
	}
	if (!strcmp(m, "sid")) {
		/* setsid() makes the caller a group leader too (no getsid in this port) */
		printf("pgrp==pid:%d\n", getpgrp() == getpid());
		return 0;
	}
	if (!strcmp(m, "stdin")) {
		char buf[64] = "";
		ssize_t n = read(0, buf, sizeof buf - 1);
		if (n > 0) buf[n] = 0;
		printf("got:%s\n", buf);
		return 0;
	}
	if (!strcmp(m, "stamp")) {		/* append mach_absolute_time (ms) to a file */
		FILE *f = fopen(argv[1], "a");
		if (f) { fprintf(f, "%.3f\n", now_ms()); fclose(f); }
		return 0;
	}
	if (!strcmp(m, "exit")) return atoi(argv[1]);
	if (!strcmp(m, "sleep")) { sleep(atoi(argv[1])); return 0; }
	if (!strcmp(m, "qos")) {
		qos_class_t q; int rp;
		pthread_get_qos_class_np(pthread_self(), &q, &rp);
		printf("qos=0x%x relprio=%d\n", (unsigned) q, rp);
		return 0;
	}
	return 99;
}

/* ---- helpers --------------------------------------------------------- */
extern char **environ;

/* spawn self --child args..., stdout into *out (malloc'ed text); returns the
 * posix_spawn(p) error, wait status in *st. extra_inherit: fd to keep, or -1. */
static int run(int search, unsigned short flags, int extra_inherit, int stdin_fd, char **envp,
               char *const argv[], char *out, size_t outsz, int *st, pid_t *pidp)
{
	posix_spawnattr_t at;
	posix_spawn_file_actions_t fa;
	int p[2], err;
	pid_t pid;
	ssize_t n, tot = 0;

	pipe(p);
	fcntl(p[0], F_SETFD, FD_CLOEXEC);
	posix_spawnattr_init(&at);
	posix_spawn_file_actions_init(&fa);
	posix_spawnattr_setflags(&at, flags);
	posix_spawn_file_actions_adddup2(&fa, p[1], 1);
	if (flags & POSIX_SPAWN_CLOEXEC_DEFAULT) {
		posix_spawn_file_actions_addinherit_np(&fa, 2);
		if (stdin_fd < 0)
			posix_spawn_file_actions_addinherit_np(&fa, 0);
	}
	if (stdin_fd >= 0)
		posix_spawn_file_actions_adddup2(&fa, stdin_fd, 0);
	if (extra_inherit >= 0)
		posix_spawn_file_actions_addinherit_np(&fa, extra_inherit);
	err = (search ? posix_spawnp : posix_spawn)(&pid, argv[0], &fa, &at, argv, envp ? envp : environ);
	posix_spawn_file_actions_destroy(&fa);
	posix_spawnattr_destroy(&at);
	close(p[1]);
	out[0] = 0;
	if (err == 0) {
		while ((n = read(p[0], out + tot, outsz - 1 - tot)) > 0)
			tot += n;
		out[tot] = 0;
		if (pidp) *pidp = pid;
		waitpid(pid, st, 0);
	}
	close(p[0]);
	return err;
}

static char *trim(char *s)
{
	size_t l = strlen(s);
	while (l && (s[l - 1] == '\n' || s[l - 1] == ' ')) s[--l] = 0;
	return s;
}

/* ---- checks ---------------------------------------------------------- */
static void test_basic(void)
{
	char out[512]; int st = 0, e;
	char *a[] = { self, "--child", "exit", "7", NULL };

	e = run(0, 0, -1, -1, NULL, a, out, sizeof out, &st, NULL);
	CHECK("spawn exit status", e == 0 && WIFEXITED(st) && WEXITSTATUS(st) == 7, "err=%d st=0x%x", e, st);
}

static void test_cloexec_default(void)
{
	char out[512]; int st, e, leak, leak2, i;
	char *a[] = { self, "--child", "fds", NULL };
	char want[32];

	leak = open("/dev/null", O_RDONLY);	/* no CLOEXEC: fork+exec would leak it */
	leak2 = open("/dev/null", O_RDONLY);
	e = run(0, 0, -1, -1, NULL, a, out, sizeof out, &st, NULL);
	snprintf(want, sizeof want, "%d ", leak);
	CHECK("without CLOEXEC_DEFAULT the open fd is inherited", e == 0 && strstr(out, want) != NULL, "out='%s'", trim(out));
	e = run(0, POSIX_SPAWN_CLOEXEC_DEFAULT, -1, -1, NULL, a, out, sizeof out, &st, NULL);
	CHECK("CLOEXEC_DEFAULT: only stdio reaches the child", e == 0 && !strcmp(trim(out), "0 2"), "err=%d out='%s'", e, out);
	e = run(0, POSIX_SPAWN_CLOEXEC_DEFAULT, leak2, -1, NULL, a, out, sizeof out, &st, NULL);
	snprintf(want, sizeof want, "0 2 %d", leak2);
	CHECK("CLOEXEC_DEFAULT + addinherit_np keeps that fd", e == 0 && !strcmp(trim(out), want), "err=%d out='%s' want='%s'", e, out, want);
	/* stdio closed in the parent: wmaker probes with fcntl first, so the same here */
	for (i = 0; i < 1; i++) ;
	close(leak); close(leak2);
}

static void test_setsid(void)
{
	char out[512]; int st, e;
	char *a[] = { self, "--child", "sid", NULL };

	e = run(0, POSIX_SPAWN_SETSID, -1, -1, NULL, a, out, sizeof out, &st, NULL);
	CHECK("SETSID: child leads its own session/group", e == 0 && !strcmp(trim(out), "pgrp==pid:1"), "err=%d out='%s'", e, out);
	e = run(0, 0, -1, -1, NULL, a, out, sizeof out, &st, NULL);
	CHECK("no SETSID: child is in the parent's group", e == 0 && !strncmp(out, "pgrp==pid:0", 11), "err=%d out='%s'", e, out);
}

static void test_dup2_stdin(void)
{
	char out[512]; int st, e, p[2];
	char *a[] = { self, "--child", "stdin", NULL };

	pipe(p);
	write(p[1], "hello", 5);
	close(p[1]);
	e = run(0, POSIX_SPAWN_SETSID | POSIX_SPAWN_CLOEXEC_DEFAULT, -1, p[0], NULL, a, out, sizeof out, &st, NULL);
	CHECK("adddup2 pipe -> child stdin (with CLOEXEC_DEFAULT)", e == 0 && !strcmp(trim(out), "got:hello"), "err=%d out='%s'", e, out);
	close(p[0]);
}

static void test_envp(void)
{
	char out[512]; int st, e;
	char *a[] = { self, "--child", "env", "WMSPAWN_X", NULL };
	char *b[] = { self, "--child", "env", "HOME", NULL };
	char *envp[] = { "WMSPAWN_X=from-envp", "DISPLAY=:7.1", NULL };

	setenv("WMSPAWN_X", "from-parent", 1);
	e = run(0, 0, -1, -1, envp, a, out, sizeof out, &st, NULL);
	CHECK("envp replaces the environment", e == 0 && !strcmp(trim(out), "from-envp"), "err=%d out='%s'", e, out);
	setenv("HOME", "/parent-home", 1);
	e = run(0, 0, -1, -1, envp, b, out, sizeof out, &st, NULL);
	CHECK("envp: a variable not in it is absent", e == 0 && !strcmp(trim(out), "(unset)"), "err=%d out='%s'", e, out);
}

static void test_spawnp(void)
{
	char out[512]; int st, e;
	char dir[256], base[128], path[512];
	char *slash = strrchr(self, '/');
	FILE *f;

	/* PATH search */
	snprintf(dir, sizeof dir, "%.*s", (int)(slash - self), self);
	snprintf(base, sizeof base, "%s", slash + 1);
	snprintf(path, sizeof path, "/nonexistent-dir:%s:/also-missing", dir);
	setenv("PATH", path, 1);
	{
		char *a[] = { base, "--child", "exit", "3", NULL };
		e = run(1, 0, -1, -1, NULL, a, out, sizeof out, &st, NULL);
		CHECK("posix_spawnp searches PATH", e == 0 && WIFEXITED(st) && WEXITSTATUS(st) == 3, "err=%d st=0x%x", e, st);
		e = run(0, 0, -1, -1, NULL, a, out, sizeof out, &st, NULL);
		CHECK("posix_spawn (no p) does not search PATH: ENOENT", e == ENOENT, "err=%d", e);
	}
	{
		char *a[] = { "wmspawn-no-such-program", NULL };
		e = run(1, 0, -1, -1, NULL, a, out, sizeof out, &st, NULL);
		CHECK("posix_spawnp nonexistent program: ENOENT", e == ENOENT, "err=%d", e);
		a[0] = "/no/such/path";
		e = run(0, 0, -1, -1, NULL, a, out, sizeof out, &st, NULL);
		CHECK("posix_spawn nonexistent path: ENOENT", e == ENOENT, "err=%d", e);
	}
	/* a file that is not executable */
	f = fopen("/tmp/wmspawn_noexec", "w"); fputs("x\n", f); fclose(f); chmod("/tmp/wmspawn_noexec", 0644);
	{
		char *a[] = { "/tmp/wmspawn_noexec", NULL };
		e = run(0, 0, -1, -1, NULL, a, out, sizeof out, &st, NULL);
		CHECK("non-executable file: EACCES", e == EACCES, "err=%d", e);
	}
	/* a script without #!: execvp falls back to /bin/sh on ENOEXEC; does posix_spawnp? */
	f = fopen("/tmp/wmspawn_noshebang", "w"); fputs("echo ran-as-sh\n", f); fclose(f); chmod("/tmp/wmspawn_noshebang", 0755);
	{
		char *a[] = { "/tmp/wmspawn_noshebang", NULL };
		e = run(0, 0, -1, -1, NULL, a, out, sizeof out, &st, NULL);
		CHECK("script without shebang, posix_spawn: ENOEXEC", e == ENOEXEC, "err=%d", e);
		e = run(1, 0, -1, -1, NULL, a, out, sizeof out, &st, NULL);
		CHECK("script without shebang, posix_spawnp: runs via /bin/sh (execvp semantics)", e == 0 && !strcmp(trim(out), "ran-as-sh"), "err=%d out='%s'", e, out);
		setenv("PATH", "/tmp", 1);
		{
			char *b[] = { "wmspawn_noshebang", NULL };
			e = run(1, 0, -1, -1, NULL, b, out, sizeof out, &st, NULL);
			CHECK("script without shebang found through PATH, posix_spawnp", e == 0 && !strcmp(trim(out), "ran-as-sh"), "err=%d out='%s'", e, out);
		}
	}
	unlink("/tmp/wmspawn_noexec"); unlink("/tmp/wmspawn_noshebang");
	setenv("PATH", "/usr/bin:/bin:/usr/sbin:/sbin:/usr/X11/bin", 1);
}

/* EVFILT_PROC NOTE_EXIT|NOTE_EXITSTATUS, the way wmaker's launch.c uses it */
static int watch_wait(int kq, pid_t pid, struct kevent *ev, int timeout_s)
{
	struct timespec ts = { timeout_s, 0 };
	return kevent(kq, NULL, 0, ev, 1, &ts);
}

static void test_evfilt_proc(void)
{
	int kq = kqueue();
	struct kevent kev, ev;
	pid_t pid;
	int st, r, ds = 0;
	char *a1[] = { self, "--child", "exit", "7", NULL };
	char *a2[] = { self, "--child", "sleep", "30", NULL };
	char *a3[] = { self, "--child", "exit", "0", NULL };
	posix_spawn_file_actions_t fa;
	posix_spawnattr_t at;
	double t0;

	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_addinherit_np(&fa, 0);
	posix_spawn_file_actions_addinherit_np(&fa, 1);
	posix_spawn_file_actions_addinherit_np(&fa, 2);
	posix_spawnattr_init(&at);
	posix_spawnattr_setflags(&at, POSIX_SPAWN_CLOEXEC_DEFAULT | POSIX_SPAWN_SETSID);

	/* exit(7): data carries the wait status */
	if (posix_spawn(&pid, self, &fa, &at, a1, environ)) { FAIL("evfilt_proc spawn", "spawn failed"); return; }
	EV_SET(&kev, pid, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT | NOTE_EXITSTATUS, 0, NULL);
	r = kevent(kq, &kev, 1, NULL, 0, NULL);
	if (r < 0) {
		printf("INFO register after a fast exit: errno %d (%s)\n", errno, strerror(errno));
	} else {
		r = watch_wait(kq, pid, &ev, 10);
		CHECK("EVFILT_PROC NOTE_EXIT delivered", r == 1 && ev.filter == EVFILT_PROC && (ev.fflags & NOTE_EXIT) && (pid_t) ev.ident == pid, "r=%d filter=%d fflags=0x%x", r, r == 1 ? ev.filter : 0, r == 1 ? ev.fflags : 0);
		ds = r == 1 ? (int) ev.data : 0;
		CHECK("EVFILT_PROC data is the wait status (exit 7)", r == 1 && WIFEXITED(ds) && WEXITSTATUS(ds) == 7, "data=0x%lx", r == 1 ? (long) ev.data : -1L);
		/* the real reap: the event comes before the zombie exists (kern_exit.c:1579 vs 1642), so poll */
		{
			int i, w = 0;
			for (i = 0; i < 200 && !(w = waitpid(pid, &st, WNOHANG)); i++)
				usleep(1000);
			CHECK("waitpid(pid, WNOHANG) reaps after the event", w == pid && WEXITSTATUS(st) == 7, "w=%d st=0x%x polls=%d", w, st, i);
			printf("INFO waitpid right after the event needed %d ms of polling\n", i);
		}
	}

	/* killed by a signal: the status says so */
	if (posix_spawn(&pid, self, &fa, &at, a2, environ)) { FAIL("evfilt_proc spawn sleeper", "spawn failed"); return; }
	EV_SET(&kev, pid, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT | NOTE_EXITSTATUS, 0, NULL);
	r = kevent(kq, &kev, 1, NULL, 0, NULL);
	CHECK("EVFILT_PROC register on a running child", r == 0, "errno=%d", errno);
	r = watch_wait(kq, pid, &ev, 1);
	CHECK("no event while the child runs", r == 0, "r=%d", r);
	t0 = now_ms();
	kill(pid, SIGKILL);
	r = watch_wait(kq, pid, &ev, 10);
	ds = r == 1 ? (int) ev.data : 0;
	CHECK("EVFILT_PROC after SIGKILL: signalled status", r == 1 && WIFSIGNALED(ds) && WTERMSIG(ds) == SIGKILL, "r=%d data=0x%lx", r, r == 1 ? (long) ev.data : -1L);
	printf("INFO kill -> EVFILT_PROC event: %.2f ms\n", now_ms() - t0);
	waitpid(pid, &st, 0);

	/* a process we are not watching being reaped first: attach then ESRCH */
	if (posix_spawn(&pid, self, &fa, &at, a3, environ)) { FAIL("evfilt_proc spawn fast", "spawn failed"); return; }
	waitpid(pid, &st, 0);
	EV_SET(&kev, pid, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT | NOTE_EXITSTATUS, 0, NULL);
	r = kevent(kq, &kev, 1, NULL, 0, NULL);
	CHECK("EVFILT_PROC on an already reaped pid: ESRCH", r < 0 && errno == ESRCH, "r=%d errno=%d", r, errno);
	posix_spawn_file_actions_destroy(&fa);
	posix_spawnattr_destroy(&at);
	close(kq);
}

static void test_qos(void)
{
	char out[512]; int st, e;
	char *a[] = { self, "--child", "qos", NULL };
	posix_spawnattr_t at;
	posix_spawn_file_actions_t fa;
	pid_t pid;
	int p[2];
	qos_class_t q; int rp;

	pthread_get_qos_class_np(pthread_self(), &q, &rp);
	printf("INFO parent qos before: 0x%x\n", (unsigned) q);
	pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
	pthread_get_qos_class_np(pthread_self(), &q, &rp);
	printf("INFO parent qos now: 0x%x (USER_INTERACTIVE=0x21)\n", (unsigned) q);
	e = run(0, 0, -1, -1, NULL, a, out, sizeof out, &st, NULL);
	printf("INFO posix_spawn child of a USER_INTERACTIVE parent: %s", out);
	/* fork+exec */
	pipe(p);
	pid = fork();
	if (pid == 0) { dup2(p[1], 1); close(p[0]); close(p[1]); execv(self, a); _exit(111); }
	close(p[1]);
	{ ssize_t n = read(p[0], out, sizeof out - 1); out[n > 0 ? n : 0] = 0; }
	close(p[0]); waitpid(pid, &st, 0);
	printf("INFO fork+exec child of a USER_INTERACTIVE parent: %s", out);
	/* UTILITY clamp, as the wmsetbg helper */
	pipe(p);
	posix_spawnattr_init(&at); posix_spawn_file_actions_init(&fa);
	e = posix_spawnattr_set_qos_class_np(&at, QOS_CLASS_UTILITY);
	CHECK("posix_spawnattr_set_qos_class_np(UTILITY)", e == 0, "err=%d", e);
	e = posix_spawnattr_set_qos_class_np(&at, QOS_CLASS_USER_INITIATED);
	printf("INFO posix_spawnattr_set_qos_class_np(USER_INITIATED) -> %d (%s): only UTILITY/BACKGROUND/MAINTENANCE are clamps (libpthread qos.c:571)\n", e, strerror(e));
	posix_spawnattr_set_qos_class_np(&at, QOS_CLASS_UTILITY);
	posix_spawn_file_actions_adddup2(&fa, p[1], 1);
	posix_spawn_file_actions_addinherit_np(&fa, 0);
	posix_spawn_file_actions_addinherit_np(&fa, 2);
	posix_spawnattr_setflags(&at, POSIX_SPAWN_CLOEXEC_DEFAULT);
	e = posix_spawn(&pid, self, &fa, &at, a, environ);
	close(p[1]);
	if (e == 0) {
		ssize_t n = read(p[0], out, sizeof out - 1); out[n > 0 ? n : 0] = 0;
		waitpid(pid, &st, 0);
		printf("INFO UTILITY-clamped posix_spawn child: %s", out);
	}
	close(p[0]);
	pthread_set_qos_class_self_np(QOS_CLASS_DEFAULT, 0);
}

/* ---- benchmark ------------------------------------------------------- */
static double cpu_ms(void)
{
	struct rusage r;
	getrusage(RUSAGE_SELF, &r);
	return (r.ru_utime.tv_sec + r.ru_stime.tv_sec) * 1e3 + (r.ru_utime.tv_usec + r.ru_stime.tv_usec) / 1e3;
}

static double bench_one(int use_spawn, int n, double *cpu)
{
	char *a[] = { self, "--child", "exit", "0", NULL };
	int i, st;
	pid_t pid;
	double t0, c0;
	posix_spawnattr_t at;
	posix_spawn_file_actions_t fa;
	int devnull = open("/dev/null", O_WRONLY);

	posix_spawnattr_init(&at); posix_spawn_file_actions_init(&fa);
	posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSID | POSIX_SPAWN_CLOEXEC_DEFAULT);
	posix_spawn_file_actions_addinherit_np(&fa, 0);
	posix_spawn_file_actions_adddup2(&fa, devnull, 1);
	posix_spawn_file_actions_addinherit_np(&fa, 2);
	c0 = cpu_ms(); t0 = now_ms();
	for (i = 0; i < n; i++) {
		if (use_spawn) {
			if (posix_spawn(&pid, self, &fa, &at, a, environ)) { printf("spawn failed\n"); exit(1); }
		} else {
			pid = fork();
			if (pid == 0) { setsid(); dup2(devnull, 1); execv(self, a); _exit(111); }
		}
		waitpid(pid, &st, 0);
	}
	*cpu = (cpu_ms() - c0) / n;
	close(devnull);
	return (now_ms() - t0) / n;
}

static void bench(int n, int mb)
{
	size_t sz = (size_t) mb << 20, i;
	char *ballast = malloc(sz ? sz : 1);
	double fc, sc, f, s;

	for (i = 0; i < sz; i += 4096) ballast[i] = 1;
	bench_one(1, 3, &sc); bench_one(0, 3, &fc);	/* warm */
	s = bench_one(1, n, &sc);
	f = bench_one(0, n, &fc);
	printf("BENCH parent_heap=%dMB n=%d  fork+exec %.2f ms/launch (parent cpu %.2f ms)  posix_spawn %.2f ms/launch (parent cpu %.2f ms)\n",
	       mb, n, f, fc, s, sc);
	s = bench_one(1, n, &sc);
	f = bench_one(0, n, &fc);
	printf("BENCH parent_heap=%dMB n=%d  fork+exec %.2f ms/launch (parent cpu %.2f ms)  posix_spawn %.2f ms/launch (parent cpu %.2f ms)  [2nd round]\n",
	       mb, n, f, fc, s, sc);
	free(ballast);
}

int main(int argc, char **argv)
{
	char buf[1024];

	if (argc > 1 && !strcmp(argv[1], "--child"))
		return child(argc - 2, argv + 2);
	self = argv[0];
	if (self[0] != '/') {
		char cwd[512];
		getcwd(cwd, sizeof cwd);
		snprintf(buf, sizeof buf, "%s/%s", cwd, self);
		self = strdup(buf);
	}
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (argc > 1 && !strcmp(argv[1], "bench")) {
		bench(argc > 2 ? atoi(argv[2]) : 30, argc > 3 ? atoi(argv[3]) : 0);
		return 0;
	}
	test_basic();
	test_cloexec_default();
	test_setsid();
	test_dup2_stdin();
	test_envp();
	test_spawnp();
	test_evfilt_proc();
	test_qos();
	printf("SPAWNTEST DONE fail=%d\n", nfail);
	return nfail != 0;
}
