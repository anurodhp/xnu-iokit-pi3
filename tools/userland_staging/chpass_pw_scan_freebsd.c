/* Real FreeBSD lib/libc/gen/pw_scan.c, adapted for chpass(1) (DAR-174).
 *
 * THE REAL BLOCKER THIS FILE CLOSES: chpass.tproj/chpass.c's own real,
 * unmodified non-OPEN_DIRECTORY LOADENTRY branch (`chpass -a`) calls
 * `__pw_scan(arg, pw, _PWSCAN_WARN|_PWSCAN_MASTER)`, and edit.c's own
 * real verify() calls `pw_scan(buf, PWSCAN_WARN|PWSCAN_MASTER)` on
 * every ordinary interactive `chpass` invocation (to validate the
 * user's edited passwd line before accepting it) -- neither symbol,
 * nor the `_PWSCAN_WARN`/`_PWSCAN_MASTER`/`PWSCAN_WARN`/`PWSCAN_MASTER`
 * constants, exist anywhere in this checkout (confirmed via exhaustive
 * grep). pwd_mkdb.tproj's OWN local pw_scan.h/pw_scan.c (already built,
 * see build_pwd_mkdb.sh) is a DIFFERENT, incompatible API --
 * `pw_scan(char *, struct passwd *, int *)`, 3rd arg an `int *` flags
 * output, vs. real FreeBSD's `__pw_scan(char *, struct passwd *, int)`
 * (plain int flags in) -- confirmed these are simply two different
 * historical BSD pw_scan lineages, not a version skew of the same one.
 *
 * Real FreeBSD's own lib/libc/gen/pw_scan.h/.c is the exact real match
 * for chpass.tproj's calling convention (`_PWSCAN_WARN`/`_PWSCAN_MASTER`
 * plus the internal `__pw_scan`/`__pw_initpwd` pair, plus lib/libutil's
 * own `pw_scan(const char*, int)` wrapper around them -- all three
 * confirmed via a real `git show` against this project's own pinned
 * third_party/freebsd-src @ release/13.2.0). Per this project's
 * source-priority rule, pulled from there rather than invented.
 *
 * ONE real, documented, and PROVEN-SAFE omission from the real upstream
 * body: every `pw->pw_fields = 0` / `pw->pw_fields |= _PWF_*` write is
 * dropped. Real Apple struct passwd (this project's iPhoneOS SDK pwd.h,
 * confirmed via a direct read) has no pw_fields member at all -- it is
 * a private Darwin/BSD extension for NIS/local-source bookkeeping, not
 * part of the public struct. Grepping the real upstream body (this
 * exact file) confirms pw_fields is WRITE-ONLY throughout -- every
 * assignment sets a bit, none of __pw_scan's own validation logic ever
 * reads pw_fields back (every branch decision uses the just-parsed
 * `p`/`id` value directly, never pw->pw_fields) -- so dropping the
 * writes changes nothing about __pw_scan's real validation behavior.
 * The only READER of pw_fields anywhere in this project's chpass build
 * is chpass.c's own non-OPEN_DIRECTORY tail (`pw->pw_fields |=
 * (old_pw->pw_fields & _PWF_SOURCE)` and the switch after it) -- and
 * that whole block is unconditionally unreachable in this exact build:
 * chpass.c's own real source has `exit(0);` as the literal first
 * statement of its non-OPEN_DIRECTORY tail (chpass.c line 388, checked
 * directly), before ANY of that code runs. That is a real, confirmed
 * bug/leftover in Apple's own pinned chpass.c independent of this port
 * (the interactive edit path validates and re-prompts for a password,
 * then exits before ever calling pw_copy()/pw_lock()/pw_mkdb() to
 * actually persist the change) -- not something this file introduces or
 * papers over; see build_chpass.sh's own header and this pass's Linear
 * comment for the full writeup. Because that reader is unreachable,
 * chpass_compat_include's own `#define pw_fields pw_gid` (see that
 * header's comment) is provably inert, not a real data hazard.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include <err.h>
#include <errno.h>
#include <pwd.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "pw_scan.h"
/* pw_dup()'s own real prototype (chpass_pw_util_freebsd.c defines it) --
 * real FreeBSD's own lib/libc/gen/pw_scan.c gets this transitively via
 * its own libc-internal headers; this project has no such internal
 * header, so pull it from the same chpass_compat_include/libutil.h
 * augmentation chpass.c/edit.c already use. */
#include <libutil.h>

/*
 * Some software assumes that IDs are short.  We should emit warnings
 * for id's which cannot be stored in a short, but we are more liberal
 * by default, warning for IDs greater than USHRT_MAX.
 */
static int	pw_big_ids_warning = 0;

void
__pw_initpwd(struct passwd *pwd)
{
	static char nul[] = "";

	memset(pwd, 0, sizeof(*pwd));
	pwd->pw_uid = (uid_t)-1;
	pwd->pw_gid = (gid_t)-1;
	pwd->pw_name = nul;
	pwd->pw_passwd = nul;
	pwd->pw_class = nul;
	pwd->pw_gecos = nul;
	pwd->pw_dir = nul;
	pwd->pw_shell = nul;
}

int
__pw_scan(char *bp, struct passwd *pw, int flags)
{
	uid_t id;
	int root;
	char *ep, *p, *sh;
	unsigned long temp;

	if (pw_big_ids_warning == -1)
		pw_big_ids_warning = getenv("PW_SCAN_BIG_IDS") == NULL ? 1 : 0;

	if (!(pw->pw_name = strsep(&bp, ":")))		/* login */
		goto fmt;
	root = !strcmp(pw->pw_name, "root");

	if (!(pw->pw_passwd = strsep(&bp, ":")))	/* passwd */
		goto fmt;

	if (!(p = strsep(&bp, ":")))			/* uid */
		goto fmt;
	if (!p[0]) {
		if (pw->pw_name[0] != '+' && pw->pw_name[0] != '-') {
			if (flags & _PWSCAN_WARN)
				warnx("no uid for user %s", pw->pw_name);
			return (0);
		}
	}
	errno = 0;
	temp = strtoul(p, &ep, 10);
	if ((temp == ULONG_MAX && errno == ERANGE) || temp > UID_MAX) {
		if (flags & _PWSCAN_WARN)
			warnx("%s > max uid value (%u)", p, UID_MAX);
		return (0);
	}
	id = temp;
	if (*ep != '\0') {
		if (flags & _PWSCAN_WARN)
			warnx("%s uid is incorrect", p);
		return (0);
	}
	if (root && id) {
		if (flags & _PWSCAN_WARN)
			warnx("root uid should be 0");
		return (0);
	}
	if (flags & _PWSCAN_WARN && pw_big_ids_warning && id > USHRT_MAX) {
		warnx("%s > recommended max uid value (%u)", p, USHRT_MAX);
		/*return (0);*/ /* THIS SHOULD NOT BE FATAL! */
	}
	pw->pw_uid = id;

	if (!(p = strsep(&bp, ":")))			/* gid */
		goto fmt;
	if (!p[0]) {
		if (pw->pw_name[0] != '+' && pw->pw_name[0] != '-') {
			if (flags & _PWSCAN_WARN)
				warnx("no gid for user %s", pw->pw_name);
			return (0);
		}
	}
	errno = 0;
	temp = strtoul(p, &ep, 10);
	if ((temp == ULONG_MAX && errno == ERANGE) || temp > GID_MAX) {
		if (flags & _PWSCAN_WARN)
			warnx("%s > max gid value (%u)", p, GID_MAX);
		return (0);
	}
	id = temp;
	if (*ep != '\0') {
		if (flags & _PWSCAN_WARN)
			warnx("%s gid is incorrect", p);
		return (0);
	}
	if (flags & _PWSCAN_WARN && pw_big_ids_warning && id > USHRT_MAX) {
		warnx("%s > recommended max gid value (%u)", p, USHRT_MAX);
		/* return (0); This should not be fatal! */
	}
	pw->pw_gid = id;

	if (flags & _PWSCAN_MASTER) {
		if (!(pw->pw_class = strsep(&bp, ":")))	/* class */
			goto fmt;

		if (!(p = strsep(&bp, ":")))		/* change */
			goto fmt;
		pw->pw_change = atol(p);

		if (!(p = strsep(&bp, ":")))		/* expire */
			goto fmt;
		pw->pw_expire = atol(p);
	}
	if (!(pw->pw_gecos = strsep(&bp, ":")))		/* gecos */
		goto fmt;

	if (!(pw->pw_dir = strsep(&bp, ":")))		/* directory */
		goto fmt;

	if (!(pw->pw_shell = strsep(&bp, ":")))		/* shell */
		goto fmt;

	p = pw->pw_shell;
	if (root && *p) {				/* empty == /bin/sh */
		for (setusershell();;) {
			if (!(sh = getusershell())) {
				if (flags & _PWSCAN_WARN)
					warnx("warning, unknown root shell");
				break;
			}
			if (!strcmp(p, sh))
				break;
		}
		endusershell();
	}

	if ((p = strsep(&bp, ":"))) {			/* too many */
fmt:
		if (flags & _PWSCAN_WARN)
			warnx("corrupted entry");
		return (0);
	}
	return (1);
}

/*
 * Wrapper around the internal functions above -- real FreeBSD
 * lib/libutil/pw_util.c's own pw_scan()/pw_initpwd() tail, reused here
 * verbatim (chpass_pw_util_freebsd.c's own header explains why THAT
 * file drops this exact tail -- it would duplicate-symbol against
 * pwd_mkdb.tproj's unrelated `pw_scan`; here there is no such
 * conflict, this is the only pw_scan()/pw_initpwd() in the chpass
 * link).
 */
void
pw_initpwd(struct passwd *pw)
{

	__pw_initpwd(pw);
}

struct passwd *
pw_scan(const char *line, int flags)
{
	struct passwd pw, *ret;
	char *bp;

	if ((bp = strdup(line)) == NULL)
		return (NULL);
	__pw_initpwd(&pw);
	if (!__pw_scan(bp, &pw, flags)) {
		free(bp);
		return (NULL);
	}
	ret = pw_dup(&pw);
	free(bp);
	return (ret);
}
