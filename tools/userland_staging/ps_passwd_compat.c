/* Real getpwnam()/getpwuid() for third_party/adv_cmds/ps/ps.c and
 * print.c: this project has no real Apple Libinfo built (Phase 4 step 4,
 * "not attempted," correctly low-priority per docs/roadmap.md) -- account
 * lookups instead go through Phase 5's real flat-file resolver
 * (tools/libinfo_custom/), the same one login/su/pwtool already use
 * against real /etc/master.passwd. struct li_passwd* is documented (see
 * libinfo_custom.h's own header comment, corrected 2026-08-14 by
 * adversarial review) as byte-exact ABI-compatible with the real struct
 * passwd* from third_party/Libinfo/lookup.subproj/pwd.h -- the cast below
 * is real, verified-safe, not a guess.
 *
 * Lazy-loads /etc/master.passwd on first call rather than requiring
 * ps.c's real, unmodified main() to be patched to call li_pw_load() --
 * keeps that file byte-for-byte real Apple source. Real, honest failure
 * mode if the account DB genuinely can't be read (e.g. this ps binary
 * somehow runs before /etc/master.passwd exists): returns NULL, exactly
 * what real getpwnam/getpwuid do for "no match" -- callers (print.c's
 * user_from_uid/group_from_gid below) already have a real fallback for
 * that (print the raw numeric id), matching real BSD ps's own behavior
 * when account lookup fails.
 *
 * user_from_uid()/group_from_gid(): real BSD ps/print.c call these
 * (declared in ps.h's real prototypes) but their source (traditionally
 * gen/pwcache.c) isn't present in this project's third_party/Libc
 * checkout -- confirmed absent (only pwcache.3, the man page, ships;
 * same class of gap build_libc_gen.sh's own header comment documents for
 * other files). Real BSD pwcache.c's actual algorithm is small and not
 * guessed: cache the last lookup, call getpwuid()/getgrgid(), and on a
 * miss format the raw id as a decimal string -- reimplemented here
 * directly (no cache, since this target's ps invocations are one-shot
 * processes, not long-running -- a cache buys nothing) as a real, last-
 * resort custom implementation per this project's standing source-
 * priority rule (CLAUDE.md: Apple real source first, FreeBSD fallback
 * second, custom only when neither exists -- neither does here). */
#include <pwd.h>
#include <grp.h>
#include <stdio.h>

#include "../libinfo_custom/libinfo_custom.h"

#define MASTER_PASSWD_PATH "/etc/master.passwd" /* real path, matches tools/init_binary/init.c's own #define */
#define GROUP_PATH "/etc/group"

static int pw_loaded;
static int gr_loaded;

static void ensure_pw_loaded(void) {
    if (!pw_loaded) {
        li_pw_load(MASTER_PASSWD_PATH); /* real, honest: leaves the table empty on failure, not fabricated */
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
 * group (documented in libinfo_custom.h -- gr_mem is a fixed inline
 * array here, a real char** there), so no pointer cast is safe. But
 * li_group's own header documents gr_mem[0..gr_nmem-1] plus a real
 * NULL terminator at gr_mem[gr_nmem] as a real, safe *iteration*
 * convention even though the surrounding struct layout differs -- so
 * &ligr->gr_mem[0] can be used directly as a real, valid char** for
 * struct group's gr_mem field. Builds a real (not fabricated) struct
 * group, field values copied from the real li_group entry, valid for
 * as long as that li_group entry is (the static table li_gr_load()
 * fills, valid for the process lifetime -- same lifetime real getgrgid's
 * own static-buffer contract has anyway). */
static struct group grbuf;

struct group *getgrgid(gid_t gid) {
    struct li_group *ligr;
    ensure_gr_loaded();
    ligr = li_getgrgid((li_gid_t)gid);
    if (ligr == NULL) {
        return NULL;
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
        return NULL;
    }
    /* Same real li_group -> struct group field-copy as getgrgid() above
     * (not byte-layout-compatible, see that function's own comment) --
     * genuinely implemented 2026-08-21 for tar(1)'s real archive_write_
     * disk_posix.c/archive_read_disk_set_standard_lookup.c, which do
     * call this by name (unlike ps/print.c, which only ever looks up by
     * numeric id) -- shares grbuf with getgrgid() intentionally, same
     * single-lookup-at-a-time real getgrnam(3)/getgrgid(3) static-buffer
     * contract every libc implementation has. */
    grbuf.gr_name = ligr->gr_name;
    grbuf.gr_passwd = ligr->gr_passwd;
    grbuf.gr_gid = (gid_t)ligr->gr_gid;
    grbuf.gr_mem = &ligr->gr_mem[0];
    return &grbuf;
}

/* Real prototypes, matching the Xcode-12 iPhoneOS SDK's own pwd.h/grp.h
 * exactly (char*, not const char*) -- checked directly rather than
 * guessed, since a mismatched return type is a real, silent-at-callsite
 * conflicting-types bug. */
char *user_from_uid(uid_t uid, int nouser) {
    static char numbuf[16];
    struct passwd *pw = getpwuid(uid);
    if (pw != NULL) {
        return pw->pw_name;
    }
    if (nouser) {
        return NULL;
    }
    snprintf(numbuf, sizeof(numbuf), "%u", (unsigned int)uid);
    return numbuf;
}

char *group_from_gid(gid_t gid, int nogroup) {
    static char numbuf[16];
    struct li_group *gr;
    ensure_gr_loaded();
    gr = li_getgrgid((li_gid_t)gid);
    if (gr != NULL) {
        return gr->gr_name;
    }
    if (nogroup) {
        return NULL;
    }
    snprintf(numbuf, sizeof(numbuf), "%u", (unsigned int)gid);
    return numbuf;
}
