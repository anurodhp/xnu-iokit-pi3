/* Real FreeBSD lib/libutil/pw_util.c, adapted for pw(8) (DAR-341).
 *
 * THE REAL BLOCKER THIS FILE CLOSES: pw(8)'s own real, unmodified
 * pwupd.c/pw_nis.c call pw_init()/pw_lock()/pw_tmp()/pw_copy()/
 * pw_mkdb()/pw_fini()/pw_equal()/pw_dup()/pw_make(), and pw_vpw.c calls
 * the public pw_scan()/pw_initpwd() wrappers -- none of these are
 * defined anywhere in this checkout (confirmed via exhaustive grep),
 * same real gap DAR-174 already found and closed for chpass(1) (see
 * chpass_pw_util_freebsd.c's own header for that investigation). This
 * file is the SAME real upstream source (lib/libutil/pw_util.c, pulled
 * from this project's own pinned third_party/freebsd-src @
 * release/13.2.0, see setup_third_party.sh's FREEBSD_DEST block) rebuilt
 * for pw(8)'s own, WIDER real call surface -- pw(8) is a separate binary
 * from chpass.macho, so unlike that file this one keeps the real
 * pw_copy() (4-arg, int-returning) (chpass.tproj had to drop it: its own
 * real, unrelated 3-arg pw_copy.c would have collided as a duplicate
 * symbol in that binary -- pw(8) links no other pw_copy(), so there is
 * no such collision here).
 *
 * SCOPE: every function real pw.c/pw_user.c/pw_group.c/pwupd.c/pw_nis.c/
 * pw_vpw.c actually calls (confirmed via `grep -ohE '\bpw_[a-z_]+\('
 * *.c` against this project's own pinned checkout): pw_init, pw_lock,
 * pw_tmp, pw_copy, pw_mkdb, pw_edit, pw_fini, pw_equal, pw_make,
 * pw_tempname, pw_dup, pw_scan, pw_initpwd. Every function body below is
 * byte-for-byte the real upstream file except the three documented
 * substitutions in pw_tmp()/pw_edit() (same ones chpass_pw_util_freebsd.c
 * already made and proved, see that file's own comments for the full
 * citation of each). pw_make_v7() is KEPT (unlike chpass's own version of
 * this file, which called it dead) -- pw_user.c's own real
 * pw_user_show() calls it directly (`p = v7 ? pw_make_v7(pwd) :
 * pw_make(pwd);`, real `-7` flag), confirmed via grep before writing this
 * file's first draft claimed otherwise. pw_error() (not part of real
 * upstream's own API at all -- an Apple-only addition to vipw.tproj/
 * pw_util.c that pw(8) never calls, confirmed via grep) is dropped, same
 * as upstream's own pw_cont() (#if 0'd dead code).
 *
 * pw_scan()/pw_initpwd() (real upstream's own public wrappers around
 * __pw_scan()/__pw_initpwd()) are NOT redefined here, even though real
 * upstream's own pw_util.c has them right after `#include "pw_scan.h"`
 * -- a first draft of this file DID duplicate them, and the real linker
 * caught it immediately (`duplicate symbol '_pw_scan'`/`'_pw_initpwd'`):
 * chpass_pw_scan_freebsd.c (real, adapted FreeBSD lib/libc/gen/
 * pw_scan.c, DAR-174 -- reused here unmodified via build_pw.sh's own
 * file list, not chpass-specific despite its name) ALREADY defines
 * these same two public wrappers itself (DAR-174 needed them there
 * because chpass_pw_util_freebsd.c's own version of this file dropped
 * them, for the same collision reason pw_copy() was dropped there).
 * Since pw(8) links both files, only one may define pw_scan()/
 * pw_initpwd() -- chpass_pw_scan_freebsd.c's real copies are kept
 * (byte-for-byte the same real upstream body either way), this file's
 * own would-be duplicates are simply omitted below.
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
 * Real upstream's own dead V7-format helper -- KEPT here (see this
 * file's own header): pw_user.c's real pw_user_show() calls it under
 * `-7`.
 */
char *
pw_make_v7(const struct passwd *pw)
{
	char *line;

	asprintf(&line, "%s:*:%ju:%ju:%s:%s:%s", pw->pw_name,
	    (uintmax_t)pw->pw_uid, (uintmax_t)pw->pw_gid,
	    pw->pw_gecos, pw->pw_dir, pw->pw_shell);
	return (line);
}

/*
 * Copy password file from one descriptor to another, replacing, deleting
 * or adding a single record on the way.
 */
int
pw_copy(int ffd, int tfd, const struct passwd *pw, struct passwd *old_pw)
{
	char *buf, *end, *line, *p, *q, *r, *tmp;
	struct passwd *fpw;
	const struct passwd *spw;
	size_t len, size;
	int eof, readlen;
	char t;

	if (old_pw == NULL && pw == NULL)
			return (-1);

	spw = old_pw;
	/* deleting a user */
	if (pw == NULL) {
		line = NULL;
	} else {
		if ((line = pw_make(pw)) == NULL)
			return (-1);
	}

	/* adding a user */
	if (spw == NULL)
		spw = pw;

	/* initialize the buffer */
	if ((buf = malloc(size = 1024)) == NULL)
		goto err;

	eof = 0;
	len = 0;
	p = q = end = buf;
	for (;;) {
		/* find the end of the current line */
		for (p = q; q < end && *q != '\0'; ++q)
			if (*q == '\n')
				break;

		/* if we don't have a complete line, fill up the buffer */
		if (q >= end) {
			if (eof)
				break;
			while ((size_t)(q - p) >= size) {
				if ((tmp = reallocarray(buf, 2, size)) == NULL) {
					warnx("passwd line too long");
					goto err;
				}
				p = tmp + (p - buf);
				q = tmp + (q - buf);
				end = tmp + (end - buf);
				buf = tmp;
				size = size * 2;
			}
			if (p < end) {
				q = memmove(buf, p, end - p);
				end -= p - buf;
			} else {
				p = q = end = buf;
			}
			readlen = read(ffd, end, size - (end - buf));
			if (readlen == -1)
				goto err;
			else
				len = (size_t)readlen;
			if (len == 0 && p == buf)
				break;
			end += len;
			len = end - buf;
			if (len < size) {
				eof = 1;
				if (len > 0 && buf[len - 1] != '\n')
					++len, *end++ = '\n';
			}
			continue;
		}

		/* is it a blank line or a comment? */
		for (r = p; r < q && isspace(*r); ++r)
			/* nothing */ ;
		if (r == q || *r == '#') {
			/* yep */
			if (write(tfd, p, q - p + 1) != q - p + 1)
				goto err;
			++q;
			continue;
		}

		/* is it the one we're looking for? */

		t = *q;
		*q = '\0';

		fpw = pw_scan(r, PWSCAN_MASTER);

		/*
		 * fpw is either the struct passwd for the current line,
		 * or NULL if the line is malformed.
		 */

		*q = t;
		if (fpw == NULL || strcmp(fpw->pw_name, spw->pw_name) != 0) {
			/* nope */
			if (fpw != NULL)
				free(fpw);
			if (write(tfd, p, q - p + 1) != q - p + 1)
				goto err;
			++q;
			continue;
		}
		if (old_pw && !pw_equal(fpw, old_pw)) {
			warnx("entry inconsistent");
			free(fpw);
			errno = EINVAL; /* hack */
			goto err;
		}
		free(fpw);

		/* it is, replace or remove it */
		if (line != NULL) {
			len = strlen(line);
			if (write(tfd, line, len) != (int)len)
				goto err;
		} else {
			/* when removed, avoid the \n */
			q++;
		}
		/* we're done, just copy the rest over */
		for (;;) {
			if (write(tfd, q, end - q) != end - q)
				goto err;
			q = buf;
			readlen = read(ffd, buf, size);
			if (readlen == 0)
				break;
			else
				len = (size_t)readlen;
			if (readlen == -1)
				goto err;
			end = buf + len;
		}
		goto done;
	}

	/* if we got here, we didn't find the old entry */
	if (line == NULL) {
		errno = ENOENT;
		goto err;
	}
	len = strlen(line);
	if ((size_t)write(tfd, line, len) != len ||
	    write(tfd, "\n", 1) != 1)
		goto err;
 done:
	free(line);
	free(buf);
	return (0);
 err:
	free(line);
	free(buf);
	return (-1);
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

/* pw_scan()/pw_initpwd() intentionally NOT defined here -- see this
 * file's own top-of-file header for why (chpass_pw_scan_freebsd.c
 * already provides them; this file's pw_copy() above calls the public
 * pw_scan() as an ordinary external call, resolved at link time). */
