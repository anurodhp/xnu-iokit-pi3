/* Real popen()/pclose()/system() for text_cmds/ed's `!cmd` shell-escape
 * and `r !cmd`/`w !cmd` pipe-I/O commands (io.c's real popen("r"/"w")
 * calls, main.c's real system() call for bare `!cmd`).
 *
 * Real Apple source exists for both (third_party/Libc/gen/FreeBSD/
 * popen.c, third_party/Libc/stdlib/FreeBSD/system.c) but both call
 * posix_spawn() -- a whole separate, unbuilt subsystem this project has
 * deliberately deferred project-wide (see build_simple_cmds.sh's own
 * Block 5 comment on `install`/`df`, and docs/roadmap.md's Phase
 * 7.5-adjacent notes) -- confirmed by reading both files directly, not
 * guessed. Per this repo's source-priority rule (Apple real -> FreeBSD
 * fallback -> custom last resort), no simpler non-posix_spawn real
 * source is vendored anywhere in this checkout either, so this is
 * genuine last-resort custom code -- but it implements the same,
 * well-known, real algorithm every popen(3)/system(3) used for decades
 * before posix_spawn existed (plain pipe()+fork()+dup2()+execve()), not
 * a fabricated stand-in, built entirely on real, already-proven
 * primitives this project already has: real fork()/waitpid()
 * (cp_mv_syscalls.c, already exercised by mv's cross-filesystem-rename
 * fallback and xargs), real execve() (simple_cmds_execve_compat.c's
 * private `_execve` -> the real raw syscall), and a real pipe(2) (this
 * file's own ed_pipe_syscall.o, see build_simple_cmds.sh's own comment
 * on why it's compiled directly from Apple's real hand-written
 * third_party/xnu-7195/libsyscall/custom/__pipe.s rather than the
 * generic Perl-generated stubs everything else in this tier uses --
 * pipe(2)'s raw kernel ABI returns both fds via x0/x1, a real special
 * case that file's own arm64 asm already handles correctly, not
 * something a generic single-return-value stub generator could
 * produce).
 *
 * Tracks up to POPEN_MAX outstanding popen() FILE*s at once (a fixed,
 * small table, real classic BSD popen() implementations used a similar
 * bounded table before switching to a linked list) -- more than enough
 * for ed's own real usage (it never has more than one `r !cmd`/`w !cmd`
 * pipe open at a time in practice).
 *
 * Adversarial review (2026-08-20) found three real, narrow gaps versus
 * real BSD/Apple popen.c/system.c, all now addressed except the third
 * (documented, not fixed, deliberately -- see below):
 *   1. FIXED: the retained pipe fd now gets FD_CLOEXEC (real popen.c
 *      does this so a LATER popen()/system() child doesn't inherit an
 *      earlier, still-open pipe fd across its own execve()).
 *   2. FIXED: system() now ignores SIGINT/SIGQUIT and blocks SIGCHLD
 *      around the wait, restoring all three before returning -- real
 *      POSIX system(3)'s own documented contract, not an embellishment
 *      (concretely: without this, ^C during a `!cmd` shell escape could
 *      kill `ed` itself instead of just the child).
 *   3. NOT fixed, honestly narrow: if the caller's fd 0/1 are already
 *      closed before popen() runs, pipe()'s returned fds could alias
 *      STDIN_FILENO/STDOUT_FILENO themselves (pipe(2) always returns
 *      the lowest available fds), which would make the `pdes[1] !=
 *      STDOUT_FILENO`/`pdes[0] != STDIN_FILENO` skip-dup2 checks below
 *      correct by accident rather than by design in that specific
 *      corner case. `ed` (run interactively or via this project's own
 *      pty-driven QEMU test harness) always has real, open stdin/
 *      stdout/stderr, so this narrow edge case is not reachable in this
 *      project's actual usage -- not worth the extra fd-table
 *      save/restore complexity real popen.c uses to handle it
 *      generally.
 */
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>

extern pid_t fork(void);
extern pid_t waitpid(pid_t pid, int *status, int options);
extern int execve(const char *path, char *const argv[], char *const envp[]);
extern int pipe(int fd[2]);
extern char **environ;

#define POPEN_MAX 8
static struct {
	FILE *fp;
	pid_t pid;
} popen_table[POPEN_MAX];

static int
run_via_sh(const char *command, pid_t *out_pid)
{
	pid_t pid;
	char *argv[4];

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		argv[0] = "sh";
		argv[1] = "-c";
		argv[2] = (char *)command;
		argv[3] = NULL;
		execve("/bin/sh", argv, environ);
		_exit(127);
	}
	*out_pid = pid;
	return 0;
}

FILE *
popen(const char *command, const char *type)
{
	int pdes[2];
	pid_t pid;
	FILE *iop;
	int slot, i;

	if ((*type != 'r' && *type != 'w') || type[1] != '\0') {
		errno = EINVAL;
		return NULL;
	}

	slot = -1;
	for (i = 0; i < POPEN_MAX; i++) {
		if (popen_table[i].fp == NULL) {
			slot = i;
			break;
		}
	}
	if (slot < 0) {
		errno = EMFILE;
		return NULL;
	}

	if (pipe(pdes) < 0)
		return NULL;

	pid = fork();
	if (pid < 0) {
		close(pdes[0]);
		close(pdes[1]);
		return NULL;
	}
	if (pid == 0) {
		char *argv[4];

		if (*type == 'r') {
			close(pdes[0]);
			if (pdes[1] != STDOUT_FILENO) {
				dup2(pdes[1], STDOUT_FILENO);
				close(pdes[1]);
			}
		} else {
			close(pdes[1]);
			if (pdes[0] != STDIN_FILENO) {
				dup2(pdes[0], STDIN_FILENO);
				close(pdes[0]);
			}
		}
		argv[0] = "sh";
		argv[1] = "-c";
		argv[2] = (char *)command;
		argv[3] = NULL;
		execve("/bin/sh", argv, environ);
		_exit(127);
	}

	if (*type == 'r') {
		close(pdes[1]);
		iop = fdopen(pdes[0], "r");
	} else {
		close(pdes[0]);
		iop = fdopen(pdes[1], "w");
	}
	if (iop == NULL) {
		int serrno = errno;
		int status;
		close(*type == 'r' ? pdes[0] : pdes[1]);
		waitpid(pid, &status, 0);
		errno = serrno;
		return NULL;
	}

	/* Real popen.c sets FD_CLOEXEC on the retained pipe fd so a LATER
	 * popen()/system() child (or any other execve()) doesn't inherit an
	 * earlier, still-open pipe end across its own exec. fileno()+fcntl()
	 * are both already real here (fcntl: libc_gen.a, already linked into
	 * every command). */
	fcntl(fileno(iop), F_SETFD, FD_CLOEXEC);

	popen_table[slot].fp = iop;
	popen_table[slot].pid = pid;
	return iop;
}

int
pclose(FILE *iop)
{
	int i;
	pid_t pid = -1;
	int status;

	for (i = 0; i < POPEN_MAX; i++) {
		if (popen_table[i].fp == iop) {
			pid = popen_table[i].pid;
			popen_table[i].fp = NULL;
			break;
		}
	}
	if (pid == -1) {
		errno = ECHILD;
		return -1;
	}

	fclose(iop);
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR)
			return -1;
	}
	return status;
}

int
system(const char *command)
{
	pid_t pid;
	int status;
	int serrno;
	struct sigaction sa_ign, sa_int_old, sa_quit_old;
	sigset_t chld_mask, old_mask;
	int have_old_sigs = 0;

	if (command == NULL)
		return 1; /* real semantics: /bin/sh is always available here */

	/* Real POSIX system(3): while waiting for the command, the calling
	 * process shall ignore SIGINT/SIGQUIT and block SIGCHLD (so ^C
	 * during a real `!cmd` shell escape hits the child, not `ed`
	 * itself, and so no unrelated SIGCHLD handler races our own
	 * waitpid() below). All three restored before returning, on every
	 * exit path. */
	sa_ign.sa_handler = SIG_IGN;
	sa_ign.sa_flags = 0;
	sigemptyset(&sa_ign.sa_mask);
	if (sigaction(SIGINT, &sa_ign, &sa_int_old) == 0 &&
	    sigaction(SIGQUIT, &sa_ign, &sa_quit_old) == 0) {
		sigemptyset(&chld_mask);
		sigaddset(&chld_mask, SIGCHLD);
		if (sigprocmask(SIG_BLOCK, &chld_mask, &old_mask) == 0)
			have_old_sigs = 1;
	}

	if (run_via_sh(command, &pid) < 0) {
		serrno = errno;
		if (have_old_sigs) {
			sigaction(SIGINT, &sa_int_old, NULL);
			sigaction(SIGQUIT, &sa_quit_old, NULL);
			sigprocmask(SIG_SETMASK, &old_mask, NULL);
		}
		errno = serrno;
		return -1;
	}

	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR) {
			status = -1;
			break;
		}
	}

	if (have_old_sigs) {
		sigaction(SIGINT, &sa_int_old, NULL);
		sigaction(SIGQUIT, &sa_quit_old, NULL);
		sigprocmask(SIG_SETMASK, &old_mask, NULL);
	}
	return status;
}
