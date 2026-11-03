/*
 * libc pieces the netbt userland calls that this port's libSystem does not
 * export (checked with nm -gU over libc_build/system/*.dylib). Each follows
 * Apple's own implementation, cited.
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <crt_externs.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

/*
 * daemon(3): third_party/Libc/gen/FreeBSD/daemon.c, minus its
 * move_to_root_bootstrap() (a launchd bootstrap-namespace move this port's
 * launchd jobs do not need: they run with -d, in the foreground).
 */
int
daemon(int nochdir, int noclose)
{
	struct sigaction osa, sa;
	int fd, oerrno, osa_ok;
	pid_t newgrp;

	sigemptyset(&sa.sa_mask);
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = 0;
	osa_ok = sigaction(SIGHUP, &sa, &osa);
	switch (fork()) {
	case -1:
		return -1;
	case 0:
		break;
	default:
		_exit(0);
	}
	newgrp = setsid();
	oerrno = errno;
	if (osa_ok != -1)
		sigaction(SIGHUP, &osa, NULL);
	if (newgrp == -1) {
		errno = oerrno;
		return -1;
	}
	if (!nochdir)
		(void)chdir("/");
	if (!noclose && (fd = open(_PATH_DEVNULL, O_RDWR, 0)) != -1) {
		(void)dup2(fd, STDIN_FILENO);
		(void)dup2(fd, STDOUT_FILENO);
		(void)dup2(fd, STDERR_FILENO);
		if (fd > 2)
			(void)close(fd);
	}
	return 0;
}

/* setprogname(3): third_party/Libc/gen/FreeBSD/setprogname.c. */
void
setprogname(const char *progname)
{
	const char *p;
	char buf[2 * MAXCOMLEN + 1];
	int mib[2];

	p = strrchr(progname, '/');
	p = p != NULL ? p + 1 : progname;
	*_NSGetProgname() = (char *)(uintptr_t)p;
	strlcpy(buf, p, sizeof(buf));
	mib[0] = CTL_KERN;
	mib[1] = KERN_PROCNAME;
	(void)sysctl(mib, 2, NULL, NULL, buf, strlen(buf));
}

/*
 * revoke(2): xnu bsd/kern/syscalls.master, syscall 56. Not exported by
 * this port's libsystem_kernel, so a raw svc (carry set = errno in x0).
 */
int
revoke(const char *path)
{
	register long x0 __asm__("x0") = (long)path;
	register long x16 __asm__("x16") = 56;
	long failed = 0;
	__asm__ volatile("svc #0x80\n b.cc 1f\n mov %0, #1\n 1:"
	    : "+r"(failed), "+r"(x0) : "r"(x16) : "memory", "cc");
	if (failed) {
		errno = (int)x0;
		return -1;
	}
	return 0;
}

/*
 * pidfile(3), NetBSD's (lib/libutil/pidfile.c in NetBSD; bthcid calls
 * pidfile(NULL)): write the pid to /var/run/<basename>.pid, the basename
 * defaulting to the program name, and remove it at exit. Apple's libutil
 * has only FreeBSD's different pidfile_open/pidfile_write API.
 */
static char pidfile_path[256];

static void
pidfile_cleanup(void)
{
	if (pidfile_path[0] != '\0')
		(void)unlink(pidfile_path);
}

int
pidfile(const char *basename)
{
	FILE *f;

	if (basename == NULL)
		basename = getprogname();
	if (basename[0] == '/')
		snprintf(pidfile_path, sizeof(pidfile_path), "%s", basename);
	else
		snprintf(pidfile_path, sizeof(pidfile_path), "/var/run/%s.pid", basename);
	if ((f = fopen(pidfile_path, "w")) == NULL) {
		pidfile_path[0] = '\0';
		return -1;
	}
	fprintf(f, "%d\n", (int)getpid());
	fclose(f);
	atexit(pidfile_cleanup);
	return 0;
}
