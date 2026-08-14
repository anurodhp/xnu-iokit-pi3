/*
 * Custom flat-file account/group resolver -- Phase 5's replacement for
 * Apple's real Libinfo (whose file_module.c backend has a real
 * pthread_mutex dependency woven through Libinfo's broader module-dispatch
 * framework -- same class of complexity this project has been avoiding,
 * see project memory project_xnu_rpi3_userspace_libc_dyld.md) and for
 * opendirectoryd (closed source entirely, no repo exists). Modeled on
 * FreeBSD's account model instead: real /etc/master.passwd + /etc/group
 * flat files, real field layout, FreeBSD's lib/libc/gen/{pw_scan.c,
 * getpwent.c,getgrent.c} used as the reference for exact field order and
 * parsing semantics (fetched fresh at doc-writing time from
 * raw.githubusercontent.com/freebsd/freebsd-src/main/lib/libc/gen/, not
 * vendored -- same "fetch real reference, don't vendor" pattern this
 * project's tools/userland_staging/build_libc_string.sh already
 * established for its own FreeBSD fallbacks).
 *
 * struct passwd / struct group layouts below are copied field-for-field
 * from this project's own real, already-cloned Apple header
 * (third_party/Libinfo/lookup.subproj/{pwd.h,grp.h}) -- real API
 * compatibility so future real code (login, su, chpass) can call these
 * functions normally, without pulling in that header directly (avoids any
 * SDK-header-skew fight for a header this simple, matching this project's
 * standing practice of typedef'ing/duplicating trivial types rather than
 * fighting header includes for freestanding code -- see
 * tools/init_binary/init.c's own header comment for the same rationale).
 *
 * Freestanding, no libc: raw syscalls only (open/read/close), self-
 * contained string/number helpers, no dependency on this project's other
 * in-progress Libc work (tools/userland_staging/ is concurrently owned by
 * a separate task this session -- this module deliberately does not link
 * against or depend on anything there).
 */

#ifndef _LIBINFO_CUSTOM_H
#define _LIBINFO_CUSTOM_H

typedef unsigned int li_uid_t;
typedef unsigned int li_gid_t;
typedef long li_time_t;

/* Field-for-field match with the real struct passwd in
 * third_party/Libinfo/lookup.subproj/pwd.h (uid_t/gid_t/__darwin_time_t
 * are all plain 32/64-bit integers on this LP64 target -- no padding/
 * layout surprises from using the li_* typedefs above instead of the
 * real SDK ones). */
struct li_passwd {
    char      *pw_name;
    char      *pw_passwd;
    li_uid_t   pw_uid;
    li_gid_t   pw_gid;
    li_time_t  pw_change;
    char      *pw_class;
    char      *pw_gecos;
    char      *pw_dir;
    char      *pw_shell;
    li_time_t  pw_expire;
};

#define LI_GROUP_MAX_MEMBERS 32

/* Field-for-field match with the real struct group in
 * third_party/Libinfo/lookup.subproj/grp.h, except gr_mem is a fixed-size
 * array here instead of a malloc'd NULL-terminated char** -- this is a
 * freestanding module with no real malloc yet (see project memory: Tier-2
 * malloc is separate, not-yet-built Libc work), so member storage is
 * static, not heap. A real caller iterates gr_mem[0..gr_nmem-1] instead of
 * until NULL -- gr_mem[gr_nmem] is always NULL too, so NULL-terminated
 * iteration also still works for compatibility with real calling code
 * expecting the real struct group's iteration convention. */
struct li_group {
    char      *gr_name;
    char      *gr_passwd;
    li_gid_t   gr_gid;
    char      *gr_mem[LI_GROUP_MAX_MEMBERS + 1];
    int        gr_nmem;
};

/* KNOWN, DELIBERATE LIMIT, flagged rather than silently accepted (found
 * during self-review): a file with more than LI_MAX_PASSWD_ENTRIES real
 * entries (or a single group line with more than LI_GROUP_MAX_MEMBERS
 * members) has its excess silently dropped -- li_pw_load()/li_gr_load()'s
 * return value is the count actually loaded, with no separate signal that
 * truncation happened, so a 65th real account would make li_getpwnam()
 * for that account indistinguishable from "account doesn't exist" rather
 * than "table full". Acceptable for now given this project's target (a
 * small embedded single-board system, not expected to need more than a
 * handful of real local accounts for the foreseeable phases) but a real
 * gap if that assumption changes later -- revisit before this resolver is
 * used somewhere a truncated account list would be a real security
 * problem (e.g. silently NOT recognizing a valid, expected account as
 * existing), not just an inconvenience. */
#define LI_MAX_PASSWD_ENTRIES 64
#define LI_MAX_GROUP_ENTRIES  64

/* Load /etc/master.passwd (or an arbitrary path, for testing) into the
 * in-memory table. Real raw-syscall file I/O (open/read/close). Returns
 * the number of real entries loaded (0 on open failure -- check errno-
 * equivalent via li_pw_load's own return convention, negative on a real
 * I/O error distinct from "file legitimately has 0 entries"), or -1 on
 * error. Must be called before any li_getpwnam/li_getpwuid call.
 * Malformed lines are skipped (matching pw_scan()'s real "corrupted
 * entry" behavior of rejecting just that line, not fatally erroring the
 * whole file) -- li_pw_load's return value is the count of entries that
 * parsed successfully, which may be less than the file's real line count. */
int li_pw_load(const char *path);
int li_gr_load(const char *path);

/* Real getpwnam/getpwuid/getgrnam/getgrgid signatures and semantics
 * (returns a pointer into the static in-memory table, valid until the
 * next li_pw_load/li_gr_load call -- same "static storage, next call
 * invalidates it" contract real getpwnam() has). NULL if not found or
 * the table hasn't been loaded yet. */
struct li_passwd *li_getpwnam(const char *name);
struct li_passwd *li_getpwuid(li_uid_t uid);
struct li_group  *li_getgrnam(const char *name);
struct li_group  *li_getgrgid(li_gid_t gid);

#endif /* _LIBINFO_CUSTOM_H */
