/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 1996
 *	David L. Nugent.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY DAVID L. NUGENT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL DAVID L. NUGENT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* Real FreeBSD usr.sbin/pw/pwupd.c, adapted for pw(8) (DAR-353).
 *
 * THE REAL BLOCKER THIS FILE CLOSES: real, unmodified FreeBSD
 * usr.sbin/pw/pwupd.c's own pwdb_check() (called by pw_update(), which
 * every one of addpwent()/chgpwent()/delpwent() calls before touching
 * /etc/master.passwd -- see pw_user.c's own perform_chgpwent() etc.)
 * unconditionally execv()s `_PATH_PWD_MKDB -C [-d etcpath]
 * <master.passwd path>` to verify the existing password database is
 * consistent before rebuilding it. Real FreeBSD's OWN sibling program,
 * usr.sbin/pwd_mkdb/pwd_mkdb.c, has a real `-C` ("check consistency
 * only") flag matching that invocation -- but this project's own
 * pwd_mkdb.macho (see build_pwd_mkdb.sh) is NOT that program: it is
 * real, unmodified Apple system_cmds/pwd_mkdb.tproj/pwd_mkdb.c (DAR-174,
 * built before pw(8)/DAR-341 existed), a completely different, older
 * real BSD lineage with its own real, unmodified getopt(3) optstring
 * `"cd:psu:v"` (see that file itself, pwd_mkdb.c:120) -- there is no
 * capital `C` anywhere in it. Confirmed by real reproduction on real
 * Raspberry Pi 3 hardware (DAR-353): `pw useradd -n <user> -m` prints
 * `/usr/sbin/pwd_mkdb: illegal option -- C` followed by that file's own
 * real usage() banner (pwd_mkdb.c:469-472), then pw_update() sees
 * pwdb_check()'s non-zero exit status and returns early without ever
 * writing the account -- so `pw useradd` no-ops with a misleading
 * `pw: passwd file update: No such file or directory` (pw_user.c:249,
 * via err() printing whatever stale errno happened to be set from an
 * earlier, unrelated, non-fatal crypt_set_format() warning at
 * pw_user.c:228 -- see pw_crypt_set_format_compat.c's own header for
 * that separate, expected, harmless warning).
 *
 * THE FIX: this project's already-built pwd_mkdb.macho's own real
 * `-c` (lowercase, "verify only" -- checkonly=1 at pwd_mkdb.c:181)
 * mode is the exact same real operation FreeBSD's `-C` requests here:
 * pwd_mkdb.c:182-186 scans every entry of the given master.passwd file
 * via scan()/pw_scan() and exit(0)s without touching the .db files,
 * i.e. "verify the password file parses cleanly, do not rebuild" --
 * identical real intent to what pwdb_check() wants before letting
 * pw_update() proceed. Below is the real, otherwise-unmodified
 * pwupd.c body (confirmed via diff against this project's own pinned
 * third_party/freebsd-src @ release/13.2.0) with exactly that one
 * argv entry changed, "-C" -> "-c", so it invokes a flag this project's
 * actual shipped pwd_mkdb.macho actually implements. This is a
 * source-level substitution (not a modification of the pinned
 * checkout itself, which build_pw.sh no longer compiles for pwupd.c --
 * see that script's own EXTRA_FILES list) so a fresh
 * setup_third_party.sh clone is untouched and this file stays the
 * single source of truth for the deviation, per this project's own
 * adaptation pattern (chpass_pw_scan_freebsd.c, pw_pw_util_freebsd.c,
 * pw_gr_util_freebsd.c, pw_crypt_set_format_compat.c all in this same
 * directory).
 *
 * The real, better long-term fix -- pulling FreeBSD's OWN real
 * usr.sbin/pwd_mkdb and building it as this project's ONE real
 * pwd_mkdb, so every caller (this file, plus vipw.tproj/chpass.tproj/
 * passwd.tproj's own real Apple pw_util.c call sites, all already
 * built against Apple's pwd_mkdb's `-p -d ... [-u user] tempname` CLI --
 * confirmed compatible via pwd_mkdb.c:120's own optstring, `c`, `d:`,
 * `p`, `s`, `u:`, `v` are ALL real, shared flags between the two real
 * lineages, only `C` (capital) is FreeBSD-pw(8)-only) all target the
 * same real CLI -- is real, larger, cross-cutting work (new
 * setup_third_party.sh sparse-checkout entry, a new build_*.sh, and
 * re-verifying vipw/chpass/passwd still link+behave against the new
 * binary) deliberately NOT done here; tracked as a follow-up rather
 * than risked in this same narrow fix.
 */
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <pwd.h>
#include <libutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pwupd.h"

char *
getpwpath(char const * file)
{
	static char pathbuf[MAXPATHLEN];

	snprintf(pathbuf, sizeof pathbuf, "%s/%s", conf.etcpath, file);

	return (pathbuf);
}

static int
pwdb_check(void)
{
	int             i = 0;
	pid_t           pid;
	char           *args[10];

	args[i++] = _PATH_PWD_MKDB;
	args[i++] = "-c";	/* DAR-353: real Apple pwd_mkdb.tproj's own
				 * "-c" (checkonly), not real FreeBSD
				 * pwd_mkdb(8)'s "-C" -- see this file's own
				 * header for the full real citation. */

	if (strcmp(conf.etcpath, _PATH_PWD) != 0) {
		args[i++] = "-d";
		args[i++] = conf.etcpath;
	}
	args[i++] = getpwpath(_MASTERPASSWD);
	args[i] = NULL;

	if ((pid = fork()) == -1)	/* Error (errno set) */
		i = errno;
	else if (pid == 0) {	/* Child */
		execv(args[0], args);
		_exit(1);
	} else {		/* Parent */
		waitpid(pid, &i, 0);
		if (WEXITSTATUS(i))
			i = EIO;
	}

	return (i);
}

static int
pw_update(struct passwd * pwd, char const * user)
{
	struct passwd	*pw = NULL;
	struct passwd	*old_pw = NULL;
	int		 rc, pfd, tfd;

	if ((rc = pwdb_check()) != 0)
		return (rc);

	if (pwd != NULL)
		pw = pw_dup(pwd);

	if (user != NULL)
		old_pw = GETPWNAM(user);

	if (pw_init(conf.etcpath, NULL))
		err(1, "pw_init()");
	if ((pfd = pw_lock()) == -1) {
		pw_fini();
		err(1, "pw_lock()");
	}
	if ((tfd = pw_tmp(-1)) == -1) {
		pw_fini();
		err(1, "pw_tmp()");
	}
	if (pw_copy(pfd, tfd, pw, old_pw) == -1) {
		pw_fini();
		close(tfd);
		err(1, "pw_copy()");
	}
	fsync(tfd);
	close(tfd);
	/*
	 * in case of deletion of a user, the whole database
	 * needs to be regenerated
	 */
	if (pw_mkdb(pw != NULL ? pw->pw_name : NULL) == -1) {
		pw_fini();
		err(1, "pw_mkdb()");
	}
	free(pw);
	pw_fini();

	return (0);
}

int
addpwent(struct passwd * pwd)
{

	return (pw_update(pwd, NULL));
}

int
chgpwent(char const * login, struct passwd * pwd)
{

	return (pw_update(pwd, login));
}

int
delpwent(struct passwd * pwd)
{

	return (pw_update(NULL, pwd->pw_name));
}
