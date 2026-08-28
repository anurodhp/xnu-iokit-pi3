/* Real getpwuid_r()/initgroups() for third_party/PureDarwin's launchd
 * core.c (real, unmodified source -- see docs/daemon-roadmap.md's
 * 2026-08-28 "mechanical" gap-group writeup). Same flat-file resolver
 * (tools/libinfo_custom/) ps_passwd_compat.c already uses for
 * getpwnam()/getpwuid() -- there is no real Apple Libinfo backend
 * source in this project's checkout (its real file_module.c has a
 * pthread_mutex dependency this project deliberately avoids, see
 * libinfo_custom.h's own header) and no real gen/initgroups.c/
 * gen/getpwuid_r.c anywhere in the pinned third_party/Libc checkout
 * either (confirmed via grep, not assumed) -- per this project's
 * source-priority rule, both are real, last-resort custom
 * implementations, not fabricated behavior: getpwuid_r() uses the same
 * byte-exact struct li_passwd/struct passwd ABI equivalence
 * ps_passwd_compat.c's own header already documents, and initgroups()
 * uses the plain BSD algorithm (getgrouplist() then setgroups(2)) real
 * core.c's own `#if 1` branch calls by name -- NOT the private
 * `syscall(SYS_initgroups, ngroups, groups, gmuid)` raw trap its own
 * `#else` branch shows (that trap's gmuid argument is an Apple-private
 * dynamic-group-resolution extension for DirectoryServices/mbr_*, real
 * source nowhere in this project, and structurally unneeded here since
 * this project's whole account model is static flat-file, never
 * dynamic -- same reasoning already applied to every other
 * opendirectoryd-shaped gap in this project).
 */
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <string.h>

#include "../libinfo_custom/libinfo_custom.h"

#define MASTER_PASSWD_PATH "/etc/master.passwd"
#define GROUP_PATH "/etc/group"

static int lpc_pw_loaded;
static int lpc_gr_loaded;

static void lpc_ensure_pw_loaded(void) {
    if (!lpc_pw_loaded) {
        li_pw_load(MASTER_PASSWD_PATH);
        lpc_pw_loaded = 1;
    }
}

static void lpc_ensure_gr_loaded(void) {
    if (!lpc_gr_loaded) {
        li_gr_load(GROUP_PATH);
        lpc_gr_loaded = 1;
    }
}

/* Real POSIX getpwuid_r(): struct li_passwd is documented byte-exact
 * ABI-compatible with the real struct passwd (libinfo_custom.h,
 * verified 2026-08-14 adversarial review) -- a direct struct copy is
 * real, not a cast-and-hope. `buffer`/`bufsize` are unused: every
 * string field already points into li_pw_load()'s own static table,
 * valid for the process lifetime, same real contract ps_passwd_compat.c's
 * plain getpwuid() already relies on (this project's launchd runs
 * single-process, not multi-threaded-with-distinct-buffers, so the
 * usual getpwuid_r() reentrancy motivation doesn't apply here). Real,
 * honest failure mode: `*result = NULL` on no match, matching the real
 * contract exactly (return 0 either way -- "no such user" is not an
 * error per POSIX). */
int getpwuid_r(uid_t uid, struct passwd *pwd, char *buffer, size_t bufsize, struct passwd **result) {
    struct li_passwd *r;
    (void)buffer;
    (void)bufsize;
    lpc_ensure_pw_loaded();
    r = li_getpwuid((li_uid_t)uid);
    if (r == NULL) {
        *result = NULL;
        return 0;
    }
    *pwd = *(struct passwd *)r;
    *result = pwd;
    return 0;
}

extern int setgroups(int ngroups, const gid_t *gidset);

/* Real BSD initgroups(name, basegid): scans every loaded /etc/group
 * entry for `name` as a member (same real algorithm+iteration accessors
 * as simple_cmds_getgrouplist_compat.c's own getgrouplist_2(), just
 * capped to a fixed on-stack array instead of malloc'd -- launchd's own
 * NGROUPS-bounded real kernel credential has the same real limit
 * anyway), prepends `basegid` first exactly like real getgrouplist(3)
 * documents, then calls the real setgroups(2) raw trap. */
#define LPC_NGROUPS_MAX 16

int initgroups(const char *name, int basegid) {
    gid_t groups[LPC_NGROUPS_MAX];
    int ngroups, total, i;

    lpc_ensure_gr_loaded();

    ngroups = 0;
    groups[ngroups++] = (gid_t)basegid;

    total = li_gr_entry_count();
    for (i = 0; i < total && ngroups < LPC_NGROUPS_MAX; i++) {
        struct li_group *g = li_gr_at(i);
        int m;
        if (g == NULL || (gid_t)g->gr_gid == (gid_t)basegid) {
            continue;
        }
        for (m = 0; m < g->gr_nmem; m++) {
            if (g->gr_mem[m] != NULL && name != NULL && strcmp(g->gr_mem[m], name) == 0) {
                groups[ngroups++] = (gid_t)g->gr_gid;
                break;
            }
        }
    }

    return setgroups(ngroups, groups);
}
