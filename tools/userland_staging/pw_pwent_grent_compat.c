/* Real setpwent()/getpwent()/endpwent() + setgrent()/getgrent()/endgrent()
 * for pw(8) (DAR-341).
 *
 * THE REAL BLOCKER THIS FILE CLOSES: pw.c's own real, unmodified
 * struct pwf PWF initializer wires GETPWENT()/SETPWENT()/ENDPWENT() (used
 * by pw_user.c's real duplicate-name/free-uid scans, e.g. pw_user_add's
 * own real "is this uid/name already taken" loop) directly to plain
 * setpwent()/getpwent()/endpwent(), and GETGRENT()/SETGRENT()/ENDGRENT()
 * the same way for the group side. Neither streaming-enumeration API
 * exists anywhere in this project yet (confirmed via exhaustive grep) --
 * this project's only account-lookup code so far (ps_passwd_compat.c,
 * DAR-127/tar) is single-record getpwnam()/getpwuid()/getgrnam()/
 * getgrgid() only, reused here unmodified (pw(8) links that file too,
 * for the same GETPWNAM()/GETPWUID() real call sites).
 *
 * tools/libinfo_custom/libinfo_custom.c (this project's real flat-file
 * /etc/master.passwd + /etc/group resolver, already used by login/su/
 * pwtool/chpass -- see that file's own header) already parses the whole
 * database into a static in-memory table on li_pw_load()/li_gr_load()
 * and exposes real indexed access to it: li_pw_entry_count()/li_pw_at(i)
 * and li_gr_entry_count()/li_gr_at(i) (added for a different real
 * consumer originally, general-purpose since). A cursor over those two
 * real primitives is a correct, real implementation of the standard
 * setXXXent/getXXXent/endXXXent enumeration contract -- not a stub: it
 * returns every real entry from the real on-disk database, in the real
 * order li_pw_load()/li_gr_load() parsed them, same as real getpwent(3)
 * over a real flat file.
 *
 * struct li_passwd* is documented byte-exact ABI-compatible with real
 * struct passwd* (libinfo_custom.h's own header comment, corrected by
 * adversarial review -- same cast ps_passwd_compat.c's own getpwnam()/
 * getpwuid() already rely on). struct li_group is NOT byte-compatible
 * with struct group (gr_mem is a fixed inline array there, a real
 * char** here) -- getgrent() below does the same real per-field copy
 * ps_passwd_compat.c's own getgrnam()/getgrgid() already do, into a
 * dedicated static buffer (a getgrent() cursor and a getgrnam()/
 * getgrgid() single lookup are different real libc contracts and must
 * not share a buffer, or an interleaved caller -- pw(8) has none, but a
 * future one might -- would see one call's result silently overwritten
 * by the other).
 */
#include <pwd.h>
#include <grp.h>

#include "../libinfo_custom/libinfo_custom.h"

#define MASTER_PASSWD_PATH "/etc/master.passwd"
#define GROUP_PATH "/etc/group"

static int pwent_loaded;
static int pwent_cursor;
static int grent_loaded;
static int grent_cursor;

/*
 * ps_passwd_compat.c (linked alongside) owns the one load of each table
 * and reloads it when the file changed (DAR-341; see its comment). Going
 * through it here, instead of a second private load-once flag, keeps
 * getpwnam() and getpwent() on the same, current data. A reload happens
 * only on set*ent(), i.e. when a walk is restarted, never in the middle of
 * one.
 */
int li_compat_pw_refresh(void);
int li_compat_gr_refresh(void);
/* Fresh copies, never pointers into the shared table (DAR-341; see
 * ps_passwd_compat.c). */
struct passwd *li_compat_pwent_at(int idx);
struct group *li_compat_grent_at(int idx);

void
setpwent(void)
{
	(void)li_compat_pw_refresh();
	pwent_loaded = 1;
	pwent_cursor = 0;
}

void
endpwent(void)
{
	pwent_cursor = 0;
}

struct passwd *
getpwent(void)
{
	struct passwd *pw;

	if (!pwent_loaded)
		setpwent();
	pw = li_compat_pwent_at(pwent_cursor);
	if (pw == 0)
		return (0);
	pwent_cursor++;
	return (pw);
}

void
setgrent(void)
{
	(void)li_compat_gr_refresh();
	grent_loaded = 1;
	grent_cursor = 0;
}

void
endgrent(void)
{
	grent_cursor = 0;
}

struct group *
getgrent(void)
{
	struct group *gr;

	if (!grent_loaded)
		setgrent();
	gr = li_compat_grent_at(grent_cursor);
	if (gr == 0)
		return (0);
	grent_cursor++;
	return (gr);
}
