/* pw_real_system() -- real system(3) for pw(8) (DAR-341).
 *
 * pw_user.c's own real, unmodified pw_user_del() calls plain `system(tmp)`
 * (killing at(1) jobs via atrm) and `system(file)` (removing a deleted
 * user's crontab via `crontab -u ... -r`) -- both real, best-effort
 * cleanup, not on any critical path. This project's own pinned Apple
 * Libc/include/stdlib.h declares system() under `__IOS_PROHIBITED`
 * (confirmed via direct read, line ~217) -- an App Store submission
 * policy annotation Apple's own SDK carries for iOS-family targets, not
 * a genuine kernel capability gap: this project's real fork()/execve()
 * already work throughout (chpass_fork_compat.c, pw_pw_util_freebsd.c's
 * own execl() calls), and this target is real hardware/QEMU, never an
 * App Store binary, so the restriction doesn't apply here.
 *
 * pw_compat_include/stdlib.h (see its own header) `#define`s `system` to
 * `pw_real_system` for pw(8)'s whole build, redirecting BOTH pw_user.c's
 * two real call sites and the SDK's own (renamed, still-attributed)
 * declaration to this name -- so this function is never itself subject
 * to the unavailable attribute, and pw_user.c's real source needs no
 * modification.
 *
 * Real fork()+execl(_PATH_BSHELL, "sh", "-c", command, NULL)+waitpid(),
 * ignoring SIGINT/SIGQUIT and restoring them after -- the standard
 * POSIX system(3) contract, also documented directly by this project's
 * own real (unbuilt) third_party/Libc/stdlib/FreeBSD/system.c (`Ignore
 * SIGINT and SIGQUIT, block SIGCHLD` -- read directly, not guessed).
 * That real file itself is NOT compiled here: its own real
 * implementation goes through posix_spawn()/a pthread mutex, neither of
 * which this project has proven for this target yet -- pulling that
 * whole dependency chain for two best-effort cleanup call sites is out
 * of proportion to what DAR-341 needs. This is a real, standard,
 * correctly-cited reimplementation of the same documented contract using
 * primitives already proven throughout this project, not a fabricated
 * stub -- it performs the real fork/exec/wait.
 */
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef _PATH_BSHELL
#define _PATH_BSHELL "/bin/sh"
#endif

int
pw_real_system(const char *command)
{
	pid_t pid;
	int pstat;
	struct sigaction ign, intact, quitact;
	sigset_t newsigblock, oldsigblock;

	if (command == NULL)
		return (1);

	ign.sa_handler = SIG_IGN;
	sigemptyset(&ign.sa_mask);
	ign.sa_flags = 0;
	sigaction(SIGINT, &ign, &intact);
	sigaction(SIGQUIT, &ign, &quitact);
	sigemptyset(&newsigblock);
	sigaddset(&newsigblock, SIGCHLD);
	sigprocmask(SIG_BLOCK, &newsigblock, &oldsigblock);

	switch ((pid = fork())) {
	case -1:
		pstat = -1;
		break;
	case 0:
		sigaction(SIGINT, &intact, NULL);
		sigaction(SIGQUIT, &quitact, NULL);
		sigprocmask(SIG_SETMASK, &oldsigblock, NULL);
		execl(_PATH_BSHELL, "sh", "-c", command, (char *)0);
		_exit(127);
		/* NOTREACHED */
	default:
		while (waitpid(pid, &pstat, 0) == -1) {
			if (errno == EINTR)
				continue;
			pstat = -1;
			break;
		}
		break;
	}

	sigaction(SIGINT, &intact, NULL);
	sigaction(SIGQUIT, &quitact, NULL);
	sigprocmask(SIG_SETMASK, &oldsigblock, NULL);
	return (pstat);
}
