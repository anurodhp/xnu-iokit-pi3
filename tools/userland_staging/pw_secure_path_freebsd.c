/* Real, unmodified FreeBSD lib/libutil/_secure_path.c (DAR-341).
 *
 * login_cap.c's own real login_getclass()/login_getpwclass() call
 * `_secure_path(userpath, pwd->pw_uid, pwd->pw_gid)` and
 * `_secure_path(path_login_conf, 0, 0)` to reject an unsafe (world-
 * writable, wrong-owned) login.conf before trusting it -- not defined
 * anywhere in this checkout (confirmed via exhaustive grep). Pulled from
 * this project's own pinned third_party/freebsd-src @ release/13.2.0
 * (lib/libutil/_secure_path.c, sparse-checked-out via
 * setup_third_party.sh's FREEBSD_DEST block). Self-contained -- only
 * real lstat()/syslog() dependencies, both already proven elsewhere in
 * this project.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <libutil.h>
#include <stddef.h>
#include <syslog.h>

/*
 * Check for common security problems on a given path
 * It must be:
 * 1. A regular file, and exists
 * 2. Owned and writable only by root (or given owner)
 * 3. Group ownership is given group or is non-group writable
 *
 * Returns:	-2 if file does not exist,
 *		-1 if security test failure
 *		0  otherwise
 */

int
_secure_path(const char *path, uid_t uid, gid_t gid)
{
    int		r = -1;
    struct stat	sb;
    const char	*msg = NULL;

    if (lstat(path, &sb) < 0) {
	if (errno == ENOENT) /* special case */
	    r = -2;  /* if it is just missing, skip the log entry */
	else
	    msg = "%s: cannot stat %s: %m";
    }
    else if (!S_ISREG(sb.st_mode))
    	msg = "%s: %s is not a regular file";
    else if (sb.st_mode & S_IWOTH)
    	msg = "%s: %s is world writable";
    else if ((int)uid != -1 && sb.st_uid != uid && sb.st_uid != 0) {
    	if (uid == 0)
    		msg = "%s: %s is not owned by root";
    	else
    		msg = "%s: %s is not owned by uid %d";
    } else if ((int)gid != -1 && sb.st_gid != gid && (sb.st_mode & S_IWGRP))
    	msg = "%s: %s is group writeable by non-authorised groups";
    else
    	r = 0;
    if (msg != NULL)
	syslog(LOG_ERR, msg, "_secure_path", path, uid);
    return r;
}
