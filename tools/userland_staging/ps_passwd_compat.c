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

/*
 * Reload when the file changed (DAR-341). Loading once per process served
 * a stale table to any program that rewrites the account files and then
 * looks the result up in the same process: pw(8) useradd wrote the new
 * account and pwd_mkdb'd it, then its own GETPWNAM() (FreeBSD
 * usr.sbin/pw/pw_user.c:1460-1462) failed with "user disappeared during
 * update". Real Apple Libinfo revalidates its flat-file cache the same way
 * -- stat() the file and compare its mtime, reloading on any change
 * (third_party/Libinfo/lookup.subproj/file_module.c:319-339, :440-444).
 * The inode and size are compared too: HFS+ keeps modification times in
 * whole seconds (contentModDate is a u_int32_t,
 * third_party/hfs/core/hfs_format.h:405), so a rewrite within the same
 * second as the first load would otherwise go unnoticed, and pwd_mkdb
 * installs the new file by rename (a new inode).
 *
 * stat64 (#338, bsd/kern/syscalls.master:518) is issued raw, like
 * libinfo_custom's own reads, so this adds no link dependency to the
 * dozen tools that share this file. On this arm64 target struct stat is
 * already the 64-bit-inode layout stat64 fills.
 */
#include <sys/stat.h>

struct li_file_stamp {
    int loaded;
    long sec, nsec;
    unsigned long long ino;
    long long size;
};

static int li_raw_stat(const char *path, struct stat *sb) {
    register long x0 asm("x0") = (long)path;
    register long x1 asm("x1") = (long)sb;
    register long x16 asm("x16") = 338; /* SYS_stat64 */
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x16)
                 : "memory", "cc");
    return is_error ? -1 : 0;
}

/* 1 if `path` must be (re)loaded: never loaded, or changed since. */
static int li_file_changed(const char *path, struct li_file_stamp *st) {
    struct stat sb;
    if (li_raw_stat(path, &sb) != 0) {
        return !st->loaded; /* cannot stat: load once, as before */
    }
    if (st->loaded && st->sec == (long)sb.st_mtimespec.tv_sec &&
        st->nsec == (long)sb.st_mtimespec.tv_nsec &&
        st->ino == (unsigned long long)sb.st_ino &&
        st->size == (long long)sb.st_size) {
        return 0;
    }
    st->sec = (long)sb.st_mtimespec.tv_sec;
    st->nsec = (long)sb.st_mtimespec.tv_nsec;
    st->ino = (unsigned long long)sb.st_ino;
    st->size = (long long)sb.st_size;
    return 1;
}

static struct li_file_stamp pw_stamp;
static struct li_file_stamp gr_stamp;

/* Also used by pw_pwent_grent_compat.c's setpwent()/setgrent(), which
 * share libinfo_custom's one passwd table and one group table. Returns 1
 * if the table was reloaded (earlier results now point at new data --
 * getpwent(3)'s "may be overwritten by subsequent calls"). */
int li_compat_pw_refresh(void) {
    if (!li_file_changed(MASTER_PASSWD_PATH, &pw_stamp)) {
        return 0;
    }
    li_pw_load(MASTER_PASSWD_PATH); /* real, honest: leaves the table empty on failure, not fabricated */
    pw_stamp.loaded = 1;
    return 1;
}

int li_compat_gr_refresh(void) {
    if (!li_file_changed(GROUP_PATH, &gr_stamp)) {
        return 0;
    }
    li_gr_load(GROUP_PATH);
    gr_stamp.loaded = 1;
    return 1;
}

static void ensure_pw_loaded(void) {
    (void)li_compat_pw_refresh();
}

static void ensure_gr_loaded(void) {
    (void)li_compat_gr_refresh();
}

/*
 * Every lookup returns a fresh copy (DAR-341). Real Apple getpwnam()
 * builds a new item from the database on each call and returns that,
 * releasing the previous one (si_user_byname() + LI_set_thread_item(),
 * third_party/Libinfo/lookup.subproj/libinfo.c:205-216; getgrnam() the
 * same, :402-410). Returning a pointer into the one shared table instead
 * let a caller's in-place edits leak into later lookups: FreeBSD pw(8)'s
 * pw_update() pw_dup()s the edited entry and then calls GETPWNAM() again
 * for the OLD one (usr.sbin/pw/pwupd.c, pw_update()), got the edited
 * struct back, and pw_copy()'s pw_equal() check failed with "entry
 * inconsistent" on every usermod/userdel. Each function has its own
 * result storage, like Libinfo's per-category thread items.
 */
#include <string.h>

/* One entry's strings. A line can be no longer than libinfo_custom's
 * whole-file buffer; 4096 bytes is far beyond any real account line and
 * a longer one fails the lookup rather than truncating it. */
#define LI_LINE_MAX_COMPAT 4096

struct li_compat_pwres {
    struct passwd pw;
    char str[LI_LINE_MAX_COMPAT];
};

struct li_compat_grres {
    struct group gr;
    char *mem[LI_GROUP_MAX_MEMBERS + 1];
    char str[LI_LINE_MAX_COMPAT];
};

static char *li_compat_put(char **cur, char *end, const char *s) {
    size_t n;
    char *d;
    if (s == NULL) {
        s = "";
    }
    n = strlen(s) + 1;
    if ((size_t)(end - *cur) < n) {
        return NULL;
    }
    d = *cur;
    memcpy(d, s, n);
    *cur += n;
    return d;
}

/* Copy a table entry into caller-owned result storage; NULL if absent or
 * (impossibly, the table lines are bounded) too long. */
static struct passwd *li_compat_pw_copy(const struct li_passwd *src, struct li_compat_pwres *res) {
    const struct passwd *spw = (const struct passwd *)src;
    char *cur = res->str, *end = res->str + sizeof(res->str);
    if (src == NULL) {
        return NULL;
    }
    res->pw = *spw;   /* li_passwd is ABI-identical to struct passwd */
    if ((res->pw.pw_name = li_compat_put(&cur, end, spw->pw_name)) == NULL ||
        (res->pw.pw_passwd = li_compat_put(&cur, end, spw->pw_passwd)) == NULL ||
        (res->pw.pw_class = li_compat_put(&cur, end, spw->pw_class)) == NULL ||
        (res->pw.pw_gecos = li_compat_put(&cur, end, spw->pw_gecos)) == NULL ||
        (res->pw.pw_dir = li_compat_put(&cur, end, spw->pw_dir)) == NULL ||
        (res->pw.pw_shell = li_compat_put(&cur, end, spw->pw_shell)) == NULL) {
        return NULL;
    }
    return &res->pw;
}

static struct group *li_compat_gr_copy(const struct li_group *src, struct li_compat_grres *res) {
    char *cur = res->str, *end = res->str + sizeof(res->str);
    int i;
    if (src == NULL) {
        return NULL;
    }
    res->gr.gr_gid = (gid_t)src->gr_gid;
    if ((res->gr.gr_name = li_compat_put(&cur, end, src->gr_name)) == NULL ||
        (res->gr.gr_passwd = li_compat_put(&cur, end, src->gr_passwd)) == NULL) {
        return NULL;
    }
    for (i = 0; i < LI_GROUP_MAX_MEMBERS && src->gr_mem[i] != NULL; i++) {
        if ((res->mem[i] = li_compat_put(&cur, end, src->gr_mem[i])) == NULL) {
            return NULL;
        }
    }
    res->mem[i] = NULL;
    res->gr.gr_mem = res->mem;
    return &res->gr;
}

struct passwd *getpwnam(const char *name) {
    static struct li_compat_pwres res;
    ensure_pw_loaded();
    return li_compat_pw_copy(li_getpwnam(name), &res);
}

struct passwd *getpwuid(uid_t uid) {
    static struct li_compat_pwres res;
    ensure_pw_loaded();
    return li_compat_pw_copy(li_getpwuid((li_uid_t)uid), &res);
}

/* getpwent()/getgrent() (pw_pwent_grent_compat.c) walk the table by
 * index; they get copies too, in their own storage. */
struct passwd *li_compat_pwent_at(int idx) {
    static struct li_compat_pwres res;
    return li_compat_pw_copy(li_pw_at(idx), &res);
}

struct group *li_compat_grent_at(int idx) {
    static struct li_compat_grres res;
    return li_compat_gr_copy(li_gr_at(idx), &res);
}

/* struct li_group is not layout-compatible with struct group (see
 * libinfo_custom.h), which the field-by-field copy above handles. */
struct group *getgrgid(gid_t gid) {
    static struct li_compat_grres res;
    ensure_gr_loaded();
    return li_compat_gr_copy(li_getgrgid((li_gid_t)gid), &res);
}

/* Also used by tar(1)'s archive_write_disk_posix.c /
 * archive_read_disk_set_standard_lookup.c (by name, unlike ps). */
struct group *getgrnam(const char *name) {
    static struct li_compat_grres res;
    ensure_gr_loaded();
    return li_compat_gr_copy(li_getgrnam(name), &res);
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
