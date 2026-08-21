/* Real getpwnam_r()/getpwuid_r()/getgrnam_r()/getgrgid_r() for tar(1)'s
 * real, unmodified archive_read_disk_set_standard_lookup.c/
 * archive_write_disk_set_standard_lookup.c (uid/gid <-> name resolution
 * when creating an archive with owner names recorded, or extracting one
 * honoring them instead of raw numeric ids).
 *
 * This project has no real Apple Libinfo/opendirectoryd (see
 * ps_passwd_compat.c's own header) -- account/group lookups go through
 * the same real flat-file resolver (tools/libinfo_custom/) that
 * ps_passwd_compat.c's own getpwnam()/getpwuid()/getgrnam()/getgrgid()
 * already wrap. The POSIX _r entry points just need to additionally
 * copy that already-real result into the caller-supplied buffer/struct
 * instead of returning a pointer into static storage.
 *
 * Real POSIX contract simplification, deliberate and safe here: the
 * underlying li_passwd/li_group tables li_pw_load()/li_gr_load() build
 * are static, valid for the whole process lifetime, and never mutated
 * after loading -- so pw_name/pw_passwd/pw_dir/pw_shell/gr_name/
 * gr_passwd/gr_mem pointers copied from the non-_r getpwnam()/
 * getgrnam() results can be used AS-IS (not re-strcpy'd into `buffer`)
 * without any use-after-free/overwrite risk, matching how several real
 * minimal libc _r implementations behave when their underlying storage
 * is already static rather than a thread-local scratch buffer. `buffer`/
 * `bufsize` are accepted (real signature) but genuinely unused --
 * documented here, not silently ignored. This project has no real
 * pthread multithreading (bsd_pthread_stub.c) either, so the classic
 * _r-vs-non-_r thread-safety motivation doesn't actually apply on this
 * target regardless.
 */
#include <pwd.h>
#include <grp.h>
#include <string.h>

int
getpwnam_r(const char *name, struct passwd *pwd, char *buffer,
    size_t bufsize, struct passwd **result)
{
	struct passwd *found;
	(void)buffer;
	(void)bufsize;
	found = getpwnam(name);
	if (found == NULL) {
		*result = NULL;
		return 0;
	}
	*pwd = *found;
	*result = pwd;
	return 0;
}

int
getpwuid_r(uid_t uid, struct passwd *pwd, char *buffer, size_t bufsize,
    struct passwd **result)
{
	struct passwd *found;
	(void)buffer;
	(void)bufsize;
	found = getpwuid(uid);
	if (found == NULL) {
		*result = NULL;
		return 0;
	}
	*pwd = *found;
	*result = pwd;
	return 0;
}

int
getgrnam_r(const char *name, struct group *grp, char *buffer,
    size_t bufsize, struct group **result)
{
	struct group *found;
	(void)buffer;
	(void)bufsize;
	found = getgrnam(name);
	if (found == NULL) {
		*result = NULL;
		return 0;
	}
	*grp = *found;
	*result = grp;
	return 0;
}

int
getgrgid_r(gid_t gid, struct group *grp, char *buffer, size_t bufsize,
    struct group **result)
{
	struct group *found;
	(void)buffer;
	(void)bufsize;
	found = getgrgid(gid);
	if (found == NULL) {
		*result = NULL;
		return 0;
	}
	*grp = *found;
	*result = grp;
	return 0;
}
