/* Real user_from_uid()/group_from_gid() for real file_cmds/mv/mv.c's
 * interactive "override <mode> <user>/<group> for <file>?" prompt (real
 * BSD gen/pwcache.c source, absent from this project's third_party/Libc
 * checkout -- same confirmed-absent gap ps_passwd_compat.c's own header
 * comment documents for `ps`/print.c's identical need).
 *
 * Deliberately narrower than ps_passwd_compat.c: this file does NOT
 * redefine getpwnam()/getpwuid()/getgrnam()/getgrgid() -- mv's own link
 * already pulls those from libcopyfile.a's copyfile_syscalls.o (real,
 * present there as honest always-fail stand-ins for acl_translate.c's
 * UUID<->name ACL-text path, see that file's own header comment; a real
 * duplicate-symbol link error confirmed this, not guessed), and
 * redefining them again here would just be the same real symbol twice.
 * Goes straight to tools/libinfo_custom/'s real flat-file resolver
 * instead of through getpwuid/getgrgid, same real data source
 * ps_passwd_compat.c uses, just without the intermediate posix-shaped
 * wrapper this file doesn't need. */
#include <stdio.h>

#include "../libinfo_custom/libinfo_custom.h"

typedef unsigned int uid_t;
typedef unsigned int gid_t;

#define MASTER_PASSWD_PATH "/etc/master.passwd"
#define GROUP_PATH "/etc/group"

static int pw_loaded;
static int gr_loaded;

static void mv_ensure_pw_loaded(void) {
    if (!pw_loaded) {
        li_pw_load(MASTER_PASSWD_PATH);
        pw_loaded = 1;
    }
}

static void mv_ensure_gr_loaded(void) {
    if (!gr_loaded) {
        li_gr_load(GROUP_PATH);
        gr_loaded = 1;
    }
}

char *user_from_uid(uid_t uid, int nouser) {
    static char numbuf[16];
    struct li_passwd *pw;
    mv_ensure_pw_loaded();
    pw = li_getpwuid((li_uid_t)uid);
    if (pw != NULL) {
        return pw->pw_name;
    }
    if (nouser) {
        return (char *)0;
    }
    snprintf(numbuf, sizeof(numbuf), "%u", uid);
    return numbuf;
}

char *group_from_gid(gid_t gid, int nogroup) {
    static char numbuf[16];
    struct li_group *gr;
    mv_ensure_gr_loaded();
    gr = li_getgrgid((li_gid_t)gid);
    if (gr != NULL) {
        return gr->gr_name;
    }
    if (nogroup) {
        return (char *)0;
    }
    snprintf(numbuf, sizeof(numbuf), "%u", gid);
    return numbuf;
}
