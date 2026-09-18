/* Real FreeBSD lib/libutil/pw_util.c, adapted for chpass(1) (DAR-174).
 *
 * THE REAL BLOCKER THIS FILE CLOSES: the pinned Apple system_cmds/
 * chpass.tproj/chpass.c's own real, unmodified non-OPEN_DIRECTORY
 * branch calls pw_init(NULL,NULL)/pw_tmp(-1)/pw_fini()/pw_tempname()/
 * pw_equal()/pw_dup()/pw_make() (edit.c) -- none of these are defined
 * anywhere in this checkout (confirmed via exhaustive grep across
 * third_party/system_cmds). Apple's own build always defines
 * OPEN_DIRECTORY (confirmed via a real `grep OPEN_DIRECTORY
 * system_cmds.xcodeproj/project.pbxproj`: GCC_PREPROCESSOR_DEFINITIONS
 * unconditionally adds it for the chpass target in both Debug and
 * Release), so this branch was apparently never build-tested by Apple
 * either -- same real finding class as build_libc_db.sh's
 * __DBINTERFACE_PRIVATE gap.
 *
 * Real Apple system_cmds/vipw.tproj/pw_util.c (already built, see
 * build_vipw.sh) is NOT a substitute: its pw_init() takes zero
 * arguments (chpass calls the real two-arg pw_init(dir, master) form),
 * and it has no pw_dup()/pw_equal()/pw_fini()/pw_make()/pw_tempname()
 * at all (confirmed by reading it directly). Per this project's
 * source-priority rule (CLAUDE.md: real Apple first, FreeBSD fallback
 * next, custom code last resort), this file pulls real FreeBSD's own
 * lib/libutil/pw_util.c instead -- its own header literally says "This
 * file is used by all the 'password' programs; vipw(8), chpass(1), and
 * passwd(1)." Pulled from this project's own pinned
 * third_party/freebsd-src @ release/13.2.0 (sparse-checked-out via
 * setup_third_party.sh's FREEBSD_DEST block, same pin already used for
 * the AES/SHA/MD5 fallbacks).
 *
 * SCOPE: only the functions chpass.tproj's own real source (chpass.c +
 * edit.c, confirmed via `grep -ohE '\bpw_[a-z_]+\(' *.c`) actually
 * calls: pw_init, pw_lock, pw_tmp, pw_mkdb, pw_edit, pw_fini, pw_equal,
 * pw_make, pw_tempname, pw_dup. Every one of these function BODIES
 * below is byte-for-byte the real upstream file (see each function's
 * own comment for the handful of documented substitutions). Real
 * upstream pw_copy()/pw_make_v7()/the pw_initpwd()/pw_scan() wrapper
 * tail are DROPPED, not because they're broken, but because
 * chpass.tproj already ships its OWN real, different-signature pw_copy
 * (pw_copy.c, 3 args vs FreeBSD's 4) and its own real pw_scan.h/
 * pwd_mkdb.tproj/pw_scan.c pairing (same project, see build_pwd_mkdb.sh)
 * -- linking either of FreeBSD's versions too would be a duplicate
 * symbol, not new coverage.
 *
 * pw_cont() (SIGCONT handler) is upstream's own #if 0'd dead code --
 * dropped for the same reason upstream itself never compiles it.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <inttypes.h>
#include <paths.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libutil.h"

/* Real FreeBSD paths.h has _PATH_ETC ("/etc") -- confirmed absent from
 * this project's real Apple iPhoneOS SDK paths.h (grepped directly),
 * needed by pw_init()'s dir==NULL branch below. Real value confirmed
 * via `git show` against this project's own pinned third_party/
 * freebsd-src @ release/13.2.0 (include/paths.h). */
#ifndef _PATH_ETC
#define _PATH_ETC "/etc"
#endif

static pid_t editpid = -1;
static int lockfd = -1;
static char masterpasswd[PATH_MAX];
static char passwd_dir[PATH_MAX];
static char tempname[PATH_MAX];
static int initialized;

/*
 * Initialize statics and set limits, signals & umask to try to avoid
 * interruptions, crashes etc. that might expose passord data.
 */
int
pw_init(const char *dir, const char *master)
{
	if (dir == NULL) {
		strcpy(passwd_dir, _PATH_ETC);
	} else {
		if (strlen(dir) >= sizeof(passwd_dir)) {
			errno = ENAMETOOLONG;
			return (-1);
		}
		strcpy(passwd_dir, dir);
	}

	if (master == NULL) {
		if (dir == NULL) {
			strcpy(masterpasswd, _PATH_MASTERPASSWD);
		} else if (snprintf(masterpasswd, sizeof(masterpasswd), "%s/%s",
		    passwd_dir, _MASTERPASSWD) > (int)sizeof(masterpasswd)) {
			errno = ENAMETOOLONG;
			return (-1);
		}
	} else {
		if (strlen(master) >= sizeof(masterpasswd)) {
			errno = ENAMETOOLONG;
			return (-1);
		}
		strcpy(masterpasswd, master);
	}

	/*
	 * The code that follows is extremely disruptive to the calling
	 * process, and is therefore disabled until someone can conceive
	 * of a realistic scenario where it would fend off a compromise.
	 * Race conditions concerning the temporary files can be guarded
	 * against in other ways than masking signals (by checking stat(2)
	 * results after creation). [Real upstream comment+#if 0 block,
	 * unchanged -- rlimit/signal/umask hardening upstream itself ships
	 * disabled.]
	 */
	initialized = 1;
	return (0);
}

/*
 * Lock the master password file.
 */
int
pw_lock(void)
{

	if (*masterpasswd == '\0')
		return (-1);

	/*
	 * If the master password file doesn't exist, the system is hosed.
	 * Might as well try to build one.  Set the close-on-exec bit so
	 * that users can't get at the encrypted passwords while editing.
	 * Open should allow flock'ing the file; see 4.4BSD.	XXX
	 */
	for (;;) {
		struct stat st;

		lockfd = flopen(masterpasswd, O_RDONLY|O_NONBLOCK|O_CLOEXEC, 0);
		if (lockfd == -1) {
			if (errno == EWOULDBLOCK) {
				errx(1, "the password db file is busy");
			} else {
				err(1, "could not lock the passwd file");
			}
		}

		/*
		 * If the password file was replaced while we were trying to
		 * get the lock, our hardlink count will be 0 and we have to
		 * close and retry.
		 */
		if (fstat(lockfd, &st) == -1)
			err(1, "fstat() failed");
		if (st.st_nlink != 0)
			break;
		close(lockfd);
		lockfd = -1;
	}
	return (lockfd);
}

/*
 * Create and open a presumably safe temp file for editing the password
 * data, and copy the master password file into it.
 */
int
pw_tmp(int mfd)
{
	char buf[8192];
	ssize_t nr;
	const char *p;
	int tfd;

	if (*masterpasswd == '\0')
		return (-1);
	if ((p = strrchr(masterpasswd, '/')))
		++p;
	else
		p = masterpasswd;
	if (snprintf(tempname, sizeof(tempname), "%.*spw.XXXXXX",
		(int)(p - masterpasswd), masterpasswd) >= (int)sizeof(tempname)) {
		errno = ENAMETOOLONG;
		return (-1);
	}
	/* Real upstream calls mkostemp(tempname, 0). This project's Apple
	 * Libc has no mkostemp() (confirmed via grep -- only mkstemp()
	 * exists, itself real Apple source, already used unmodified by
	 * vipw.tproj's own real pw_util.c). Per mkostemp(3)'s own real
	 * documented contract, passing oflags=0 is defined as identical to
	 * plain mkstemp() -- not a behavior change, a substitution of two
	 * calls the man page itself says are equivalent for this exact
	 * argument. */
	if ((tfd = mkstemp(tempname)) == -1)
		return (-1);
	if (mfd != -1) {
		while ((nr = read(mfd, buf, sizeof(buf))) > 0)
			if (write(tfd, buf, (size_t)nr) != nr)
				break;
		if (nr != 0) {
			unlink(tempname);
			*tempname = '\0';
			close(tfd);
			return (-1);
		}
	}
	return (tfd);
}

/*
 * Regenerate the password database.
 */
int
pw_mkdb(const char *user)
{
	int pstat;
	pid_t pid;

	(void)fflush(stderr);
	switch ((pid = fork())) {
	case -1:
		return (-1);
	case 0:
		/* child */
		if (user == NULL)
			execl(_PATH_PWD_MKDB, "pwd_mkdb", "-p",
			    "-d", passwd_dir, tempname, (char *)NULL);
		else
			execl(_PATH_PWD_MKDB, "pwd_mkdb", "-p",
			    "-d", passwd_dir, "-u", user, tempname,
			    (char *)NULL);
		_exit(1);
		/* NOTREACHED */
	default:
		/* parent */
		break;
	}
	if (waitpid(pid, &pstat, 0) == -1)
		return (-1);
	if (WIFEXITED(pstat) && WEXITSTATUS(pstat) == 0)
		return (0);
	errno = 0;
	return (-1);
}

/*
 * Edit the temp file.  Return -1 on error, >0 if the file was modified, 0
 * if it was not.
 */
int
pw_edit(int notsetuid)
{
	struct sigaction sa, sa_int, sa_quit;
	sigset_t oldsigset, nsigset;
	struct stat st1, st2;
	const char *editor;
	int pstat;

	if ((editor = getenv("EDITOR")) == NULL)
		editor = _PATH_VI;
	if (stat(tempname, &st1) == -1)
		return (-1);
	sa.sa_handler = SIG_IGN;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGINT, &sa, &sa_int);
	sigaction(SIGQUIT, &sa, &sa_quit);
	sigemptyset(&nsigset);
	sigaddset(&nsigset, SIGCHLD);
	sigprocmask(SIG_BLOCK, &nsigset, &oldsigset);
	switch ((editpid = fork())) {
	case -1:
		return (-1);
	case 0:
		sigaction(SIGINT, &sa_int, NULL);
		sigaction(SIGQUIT, &sa_quit, NULL);
		sigprocmask(SIG_SETMASK, &oldsigset, NULL);
		if (notsetuid) {
			if (setgid(getgid()) == -1)
				err(1, "setgid");
			if (setuid(getuid()) == -1)
				err(1, "setuid");
		}
		execlp(editor, editor, tempname, (char *)NULL);
		err(1, "%s", editor);
	default:
		/* parent */
		break;
	}
	for (;;) {
		if (waitpid(editpid, &pstat, WUNTRACED) == -1) {
			if (errno == EINTR)
				continue;
			unlink(tempname);
			editpid = -1;
			break;
		} else if (WIFSTOPPED(pstat)) {
			raise(WSTOPSIG(pstat));
		} else if (WIFEXITED(pstat)) {
			if (WEXITSTATUS(pstat) != 0)
				errx(1, "\"%s\" exited with status %d", editor, WEXITSTATUS(pstat));
			editpid = -1;
			break;
		} else {
			unlink(tempname);
			editpid = -1;
			break;
		}
	}
	sigaction(SIGINT, &sa_int, NULL);
	sigaction(SIGQUIT, &sa_quit, NULL);
	sigprocmask(SIG_SETMASK, &oldsigset, NULL);
	if (stat(tempname, &st2) == -1)
		return (-1);
	/* Real upstream compares st1.st_mtim/st2.st_mtim (Linux/FreeBSD-
	 * style struct stat field name). This project's real Apple
	 * iPhoneOS SDK <sys/stat.h> names the identical field
	 * st_mtimespec instead (confirmed via grep -- same struct timespec
	 * type, same semantics, different field name only) -- substituted
	 * here, not a behavior change. */
	return (st1.st_mtimespec.tv_sec != st2.st_mtimespec.tv_sec ||
	    st1.st_mtimespec.tv_nsec != st2.st_mtimespec.tv_nsec);
}

/*
 * Clean up.  Preserve errno for the caller's convenience.
 */
void
pw_fini(void)
{
	int serrno, status;

	if (!initialized)
		return;
	initialized = 0;
	serrno = errno;
	if (editpid != -1) {
		kill(editpid, SIGTERM);
		kill(editpid, SIGCONT);
		waitpid(editpid, &status, 0);
		editpid = -1;
	}
	if (*tempname != '\0') {
		unlink(tempname);
		*tempname = '\0';
	}
	if (lockfd != -1)
		close(lockfd);
	errno = serrno;
}

/*
 * Compares two struct pwds.
 */
int
pw_equal(const struct passwd *pw1, const struct passwd *pw2)
{
	return (strcmp(pw1->pw_name, pw2->pw_name) == 0 &&
	    pw1->pw_uid == pw2->pw_uid &&
	    pw1->pw_gid == pw2->pw_gid &&
	    strcmp(pw1->pw_class, pw2->pw_class) == 0 &&
	    pw1->pw_change == pw2->pw_change &&
	    pw1->pw_expire == pw2->pw_expire &&
	    strcmp(pw1->pw_gecos, pw2->pw_gecos) == 0 &&
	    strcmp(pw1->pw_dir, pw2->pw_dir) == 0 &&
	    strcmp(pw1->pw_shell, pw2->pw_shell) == 0);
}

/*
 * Make a passwd line out of a struct passwd.
 */
char *
pw_make(const struct passwd *pw)
{
	char *line;

	asprintf(&line, "%s:%s:%ju:%ju:%s:%ju:%ju:%s:%s:%s", pw->pw_name,
	    pw->pw_passwd, (uintmax_t)pw->pw_uid, (uintmax_t)pw->pw_gid,
	    pw->pw_class, (uintmax_t)pw->pw_change, (uintmax_t)pw->pw_expire,
	    pw->pw_gecos, pw->pw_dir, pw->pw_shell);
	return (line);
}

/*
 * Return the current value of tempname.
 */
const char *
pw_tempname(void)
{

	return (tempname);
}

/*
 * pw_error() -- NOT part of real FreeBSD's lib/libutil/pw_util.c (that
 * API dropped it; FreeBSD's own chpass/vipw callers use err(3)/errx(3)
 * directly instead). This project's real chpass.tproj/util.c DOES
 * define a pw_error(char *, int, int) with this same signature, but
 * ONLY inside its own `#if OPEN_DIRECTORY` guard (confirmed by reading
 * util.c directly: the guard opens at its own line 224 and doesn't
 * close until end-of-file) -- so in this project's real
 * non-OPEN_DIRECTORY build, util.c provides NO pw_error definition at
 * all, even though chpass.tproj's own real pw_copy.c (3-arg pw_copy,
 * already built here, see build_chpass.sh) unconditionally calls
 * pw_error() at multiple real error paths. Real Apple system_cmds/
 * vipw.tproj/pw_util.c (already built, see build_vipw.sh) has the exact
 * same real signature and body -- reused verbatim here (not
 * reimplemented) since it needs this file's own `tempname` static in
 * scope, which vipw's copy has its own separate instance of (can't be
 * pulled in as a whole separate .o without a duplicate-symbol conflict
 * against every other pw_* function above).
 */
void
pw_error(char *name, int err, int eval)
{
	if (err)
		warn("%s", name);

	warnx("%s: unchanged", _PATH_MASTERPASSWD);
	(void)unlink(tempname);
	exit(eval);
}

/*
 * Duplicate a struct passwd.
 */
struct passwd *
pw_dup(const struct passwd *pw)
{
	char *dst;
	struct passwd *npw;
	ssize_t len;

	len = sizeof(*npw);
	if (pw->pw_name != NULL)
		len += strlen(pw->pw_name) + 1;
	if (pw->pw_passwd != NULL)
		len += strlen(pw->pw_passwd) + 1;
	if (pw->pw_class != NULL)
		len += strlen(pw->pw_class) + 1;
	if (pw->pw_gecos != NULL)
		len += strlen(pw->pw_gecos) + 1;
	if (pw->pw_dir != NULL)
		len += strlen(pw->pw_dir) + 1;
	if (pw->pw_shell != NULL)
		len += strlen(pw->pw_shell) + 1;
	if ((npw = malloc((size_t)len)) == NULL)
		return (NULL);
	memcpy(npw, pw, sizeof(*npw));
	dst = (char *)npw + sizeof(*npw);
	if (pw->pw_name != NULL) {
		npw->pw_name = dst;
		dst = stpcpy(npw->pw_name, pw->pw_name) + 1;
	}
	if (pw->pw_passwd != NULL) {
		npw->pw_passwd = dst;
		dst = stpcpy(npw->pw_passwd, pw->pw_passwd) + 1;
	}
	if (pw->pw_class != NULL) {
		npw->pw_class = dst;
		dst = stpcpy(npw->pw_class, pw->pw_class) + 1;
	}
	if (pw->pw_gecos != NULL) {
		npw->pw_gecos = dst;
		dst = stpcpy(npw->pw_gecos, pw->pw_gecos) + 1;
	}
	if (pw->pw_dir != NULL) {
		npw->pw_dir = dst;
		dst = stpcpy(npw->pw_dir, pw->pw_dir) + 1;
	}
	if (pw->pw_shell != NULL) {
		npw->pw_shell = dst;
		dst = stpcpy(npw->pw_shell, pw->pw_shell) + 1;
	}
	return (npw);
}
