/* Real Apple daemon(3) (third_party/Libc/gen/FreeBSD/daemon.c) needs
 * <servers/bootstrap.h> for move_to_root_bootstrap() -- re-pointing the
 * caller's Mach bootstrap port at the root (launchd) bootstrap namespace
 * before backgrounding, so a daemonized process doesn't stay attached to
 * whatever per-session bootstrap its interactive parent had. This
 * project has no launchd and no bootstrap namespace hierarchy at all
 * (same real scope decision as third_party-patches/cron.patch's
 * do_command.c changes), so that header/step doesn't apply here.
 *
 * cron.c's only call site is `getppid() != 1 && daemon(1, 0) == -1`
 * (Apple's own "don't daemonize when run by launchd" check) -- this
 * project's custom PID-1 (tools/init_binary/init.c) is always cron's
 * direct parent, so getppid() is always 1 and daemon() is provably never
 * actually called at runtime, not just assumed unreachable. Implemented
 * for real anyway (real fork/setsid/chdir/fd-redirect, the portable BSD
 * daemon(3) behavior minus the bootstrap-port step) rather than a stub
 * that fails, so any future caller in a different process tree still
 * gets genuine daemonization.
 */
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

int
daemon(int nochdir, int noclose)
{
	switch (fork()) {
	case -1:
		return -1;
	case 0:
		break;
	default:
		_exit(0);
	}

	if (setsid() == -1) {
		return -1;
	}

	if (!nochdir && chdir("/") == -1) {
		return -1;
	}

	if (!noclose) {
		int fd = open("/dev/null", O_RDWR);
		if (fd == -1) {
			return -1;
		}
		dup2(fd, STDIN_FILENO);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		if (fd > STDERR_FILENO) {
			close(fd);
		}
	}

	return 0;
}
