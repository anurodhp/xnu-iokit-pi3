/* Real getpwnam()/getpwuid()/getgrnam()/getgrgid() for ls's own
 * acl_translate.c (DAR-148): acl_from_text()/uuid_to_name() call these
 * directly to convert between ACL-text usernames/group names and real
 * uid_t/gid_t/uuid_t values - a genuinely new need, distinct from ls's
 * pre-existing `-l` username column, which already gets real names via
 * mv_pwcache_compat.c's user_from_uid()/group_from_gid() (also linked
 * into ls).
 *
 * Deliberately narrower than ps_passwd_compat.c, which this file's
 * bodies are copied from verbatim (same real tools/libinfo_custom/
 * flat-file resolver against /etc/master.passwd and /etc/group, same
 * struct li_passwd/li_group byte-layout-compatibility citation that
 * file's own header comment gives in full): this file does NOT
 * redefine user_from_uid()/group_from_gid(), since mv_pwcache_compat.c
 * already provides both and a real duplicate-symbol link error
 * confirmed the two cannot coexist unmodified in the same link, not
 * guessed.
 */
#include <pwd.h>
#include <grp.h>
#include <stdio.h>

#include "../libinfo_custom/libinfo_custom.h"

#define MASTER_PASSWD_PATH "/etc/master.passwd"
#define GROUP_PATH "/etc/group"

static int pw_loaded;
static int gr_loaded;

static void ensure_pw_loaded(void) {
    if (!pw_loaded) {
        li_pw_load(MASTER_PASSWD_PATH);
        pw_loaded = 1;
    }
}

static void ensure_gr_loaded(void) {
    if (!gr_loaded) {
        li_gr_load(GROUP_PATH);
        gr_loaded = 1;
    }
}

struct passwd *getpwnam(const char *name) {
    ensure_pw_loaded();
    return (struct passwd *)li_getpwnam(name);
}

struct passwd *getpwuid(uid_t uid) {
    ensure_pw_loaded();
    return (struct passwd *)li_getpwuid((li_uid_t)uid);
}

/* Real struct li_group is NOT byte-layout-compatible with real struct
 * group (gr_mem is a fixed inline array here, a real char** there) --
 * see ps_passwd_compat.c's own getgrgid()/getgrnam() comment for the
 * full citation. Builds a real (not fabricated) struct group, copied
 * field-by-field, valid for as long as the underlying li_group entry
 * is (the static table li_gr_load() fills, valid for process
 * lifetime). */
static struct group grbuf;

struct group *getgrgid(gid_t gid) {
    struct li_group *ligr;
    ensure_gr_loaded();
    ligr = li_getgrgid((li_gid_t)gid);
    if (ligr == NULL) {
        return (struct group *)0;
    }
    grbuf.gr_name = ligr->gr_name;
    grbuf.gr_passwd = ligr->gr_passwd;
    grbuf.gr_gid = (gid_t)ligr->gr_gid;
    grbuf.gr_mem = &ligr->gr_mem[0];
    return &grbuf;
}

struct group *getgrnam(const char *name) {
    struct li_group *ligr;
    ensure_gr_loaded();
    ligr = li_getgrnam(name);
    if (ligr == NULL) {
        return (struct group *)0;
    }
    grbuf.gr_name = ligr->gr_name;
    grbuf.gr_passwd = ligr->gr_passwd;
    grbuf.gr_gid = (gid_t)ligr->gr_gid;
    grbuf.gr_mem = &ligr->gr_mem[0];
    return &grbuf;
}
