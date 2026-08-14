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
 * ABI-compatibility claim CORRECTED 2026-08-14 (adversarial review): only
 * struct li_passwd is actually byte-exact with the real struct passwd in
 * third_party/Libinfo/lookup.subproj/pwd.h (verified) -- struct li_group
 * is NOT byte-compatible with the real struct group in that same header's
 * grp.h (gr_mem here is an inline char*[LI_GROUP_MAX_MEMBERS+1] array,
 * sizeof 296 on LP64; the real struct group's gr_mem is a plain char**,
 * sizeof 32 total for the struct). See struct li_group's own comment
 * below for why, and do NOT cast a struct li_group* to struct group* or
 * vice versa -- only struct li_passwd* is safe to treat as
 * ABI-equivalent to a real struct passwd*.
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

/* NOT ABI-compatible with the real struct group in
 * third_party/Libinfo/lookup.subproj/grp.h (corrected 2026-08-14, see the
 * file header's own correction above) -- gr_name/gr_passwd/gr_gid match
 * field-for-field, but gr_mem here is a fixed-size inline array instead
 * of the real struct's malloc'd NULL-terminated char** (this is a
 * freestanding module with no real malloc yet, see project memory:
 * Tier-2 malloc is separate, not-yet-built Libc work, so member storage
 * is static, not heap) -- real callers must use struct li_group
 * specifically via this header, not assume a raw pointer/size
 * reinterpretation of a real struct group works. A real caller iterates
 * gr_mem[0..gr_nmem-1], or can rely on gr_mem[gr_nmem] always being NULL
 * for NULL-terminated-style iteration too -- that iteration convention is
 * compatible even though the memory layout isn't. */
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
 * in-memory table. Real raw-syscall file I/O (open/read/close).
 *
 * Return value CORRECTED 2026-08-14 (adversarial review: the previous
 * wording here claimed distinguishable "0 on open failure" vs "negative
 * on a real I/O error" return codes -- false, the real implementation
 * returns exactly -1 for BOTH an open failure and a read failure, no
 * finer-grained signal). Real, accurate contract: returns -1 on any
 * open/read failure (the in-memory table is reset to empty in this case,
 * not left stale -- see the .c file's own P1 fix comment), or a
 * non-negative count (>= 0) of entries that parsed successfully
 * otherwise. Must be called before any li_getpwnam/li_getpwuid call.
 * Malformed lines are skipped (matching pw_scan()'s real "corrupted
 * entry" behavior of rejecting just that line, not fatally erroring the
 * whole file) -- the returned count may be less than the file's real
 * line count, both because of skipped malformed lines and because of the
 * LI_MAX_PASSWD_ENTRIES/LI_MAX_GROUP_ENTRIES capacity limit above.
 *
 * KNOWN, DELIBERATE LIMITS not yet fixed (found during review, documented
 * rather than silently shipped):
 *  - a NUL byte embedded in a line truncates parsing of that line (and
 *    everything after it in the file, since this module's C-string-based
 *    parsing has no way to represent a length distinct from a NUL
 *    terminator) at that point -- inherent to the design, not planned to
 *    be fixed without a real switch to length-prefixed parsing.
 *  - a literal negative uid/gid (real BSD convention for e.g. `nobody`,
 *    conventionally uid/gid -2) is rejected outright: li_strtoul10 has no
 *    '-' handling, by design, to keep the overflow-safety guarantee
 *    simple and unambiguous for the normal (non-negative) case. A real
 *    /etc/master.passwd `nobody:*:-2:-2::0:0:Unprivileged User:/var/empty:
 *    /usr/bin/false` line is silently DROPPED (treated as malformed, like
 *    any other unparseable line), not given some fallback value -- a
 *    real, deliberate decision, not an oversight, but worth knowing
 *    before assuming a standard `nobody` entry will be usable as-is. */
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
