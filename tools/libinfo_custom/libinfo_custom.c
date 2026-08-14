/* Implementation. See libinfo_custom.h for the full design rationale. */

#include "libinfo_custom.h"

typedef unsigned long li_size_t;

/* ---------------- raw syscalls (arm64 Darwin: svc #0x80, num in x16) ----
 * Same convention already established and proven correct by
 * tools/init_binary/init.c (carry-flag-means-error, not negated return) --
 * numbers cross-checked directly against
 * third_party/xnu-7195/bsd/kern/syscalls.master, not guessed:
 * exit=1 read=3 write=4 open=5 close=6. */

#define LI_SYS_EXIT  1
#define LI_SYS_READ  3
#define LI_SYS_WRITE 4
#define LI_SYS_OPEN  5
#define LI_SYS_CLOSE 6
#define LI_O_RDONLY  0

#ifdef LIBINFO_HOST_TEST
/* Host-side logic testing only -- real freestanding statically-linked
 * Mach-O binaries get killed by this host's own hardened-runtime
 * enforcement before main() ever runs (confirmed independently this same
 * session, see project_xnu_rpi3_userspace_libc_dyld.md), so raw `svc`
 * instructions issued directly from a normal host process are not a
 * viable host-test path here -- forward to real host libc instead,
 * same host_shim_syscall3 pattern tools/init_binary/init.c's own
 * documented host-test harness (README.md) already established. */
long li_host_shim_open(const char *path, int flags, int mode);
long li_host_shim_read(int fd, void *buf, li_size_t n);
long li_host_shim_close(int fd);

static long li_sys_open(const char *path, int flags, int mode) {
    return li_host_shim_open(path, flags, mode);
}
static long li_sys_read(int fd, void *buf, li_size_t n) {
    return li_host_shim_read(fd, buf, n);
}
static long li_sys_close(int fd) {
    return li_host_shim_close(fd);
}
#else
static inline long li_syscall3(long num, long a0, long a1, long a2) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x2), "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        return -x0;
    }
    return x0;
}
static long li_sys_open(const char *path, int flags, int mode) {
    return li_syscall3(LI_SYS_OPEN, (long)path, flags, mode);
}
static long li_sys_read(int fd, void *buf, li_size_t n) {
    return li_syscall3(LI_SYS_READ, fd, (long)buf, (long)n);
}
static long li_sys_close(int fd) {
    return li_syscall3(LI_SYS_CLOSE, fd, 0, 0);
}
#endif

/* ------------------------------- tiny string/number helpers -------------
 * Self-contained, no libc -- same rationale as tools/init_binary/init.c's
 * kstrlen/kstreq/etc: this module must not depend on this project's other,
 * concurrently in-progress Libc work. */

static int li_streq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

/* Real strsep() semantics (BSD): split *sp at the first byte in `delim`,
 * NUL-terminate the token in place, advance *sp past it (NULL if this was
 * the last token). Returns NULL only if *sp was already NULL (matches
 * real strsep() exactly -- this is the same primitive pw_scan.c/
 * getgrent.c's real parsing logic is built on, adapted here rather than
 * reinvented with different semantics). */
static char *li_strsep(char **sp, char delim) {
    char *start = *sp;
    char *p;
    if (start == 0) {
        return 0;
    }
    for (p = start; *p; p++) {
        if (*p == delim) {
            *p = 0;
            *sp = p + 1;
            return start;
        }
    }
    *sp = 0;
    return start;
}

/* Real strtoul()-equivalent, base 10 only (all this file's numeric fields
 * -- uid/gid -- are decimal, matching pw_scan.c's real strtoul(p, &ep, 10)
 * call). Sets *ok=0 on empty input, any non-digit trailing character
 * (matching pw_scan.c's own "if (*ep != '\0') { corrupted }" rejection --
 * a partially-numeric field like "12x" is a real parse error, not
 * silently truncated to 12), OR on accumulator overflow (matching
 * pw_scan.c's own real
 * "if ((temp == ULONG_MAX && errno == ERANGE) || temp > UID_MAX)" check --
 * found and fixed during self-review before this file's first commit: an
 * earlier version had no overflow guard at all, so a corrupted or
 * malicious oversized numeric field (e.g. a master.passwd line with a
 * 30-digit "uid") would silently wrap the accumulator and, worse, get
 * silently narrowed to the 32-bit li_uid_t/li_gid_t at the call site --
 * real privilege-relevant memory corruption class of bug for
 * security-adjacent code like this, not just a cosmetic parse issue). */
static unsigned long li_strtoul10(const char *p, int *ok) {
    unsigned long v = 0;
    const char *start = p;
    if (!p || !*p) {
        *ok = 0;
        return 0;
    }
    while (*p) {
        if (*p < '0' || *p > '9') {
            *ok = 0;
            return 0;
        }
        unsigned long digit = (unsigned long)(*p - '0');
        unsigned long nv = v * 10 + digit;
        if (nv < v || (nv - digit) / 10 != v) { /* overflow of unsigned long itself */
            *ok = 0;
            return 0;
        }
        v = nv;
        p++;
    }
    if (v > 0xFFFFFFFFUL) { /* li_uid_t/li_gid_t are 32-bit -- reject anything
                              * that wouldn't survive the narrowing cast at
                              * the call site intact, same real bound
                              * pw_scan.c's own UID_MAX/GID_MAX check enforces */
        *ok = 0;
        return 0;
    }
    *ok = (p != start);
    return v;
}

/* ------------------------------- file loading ---------------------------
 * Whole-file-into-memory, matching this project's other freestanding
 * userland code's general style (no streaming/incremental parsing
 * needed for account files, which are small). Fixed-size static buffers
 * -- no malloc yet (Tier-2 malloc is separate, not-yet-built Libc work,
 * see project memory) -- sized generously for a small local-account
 * file, not general-purpose. */

#define LI_FILE_BUF_SIZE 16384

static char li_pw_filebuf[LI_FILE_BUF_SIZE];
static char li_gr_filebuf[LI_FILE_BUF_SIZE];

/* Returns real byte count read into buf (NUL-terminated), or a negative
 * raw errno-style value on a real open/read failure. *truncated is set
 * to 1 iff the real file is >= bufsize - 1 bytes, i.e. there may be more
 * real file content than fit in buf -- distinct from a real file that
 * happens to end exactly at EOF with no trailing newline, which is NOT
 * truncation and must still have its last line parsed normally. Callers
 * use this to discard only a genuinely-incomplete final line, not to
 * reject every file whose last line lacks a trailing '\n'.
 *
 * CORRECTED 2026-08-14 (adversarial review): an earlier version of this
 * function's own doc comment claimed a truncated final line would "fail
 * li_pw_parse_line's field-count check and just be skipped" -- false,
 * and disproven with a real fixture (a file cut mid-shell-field was
 * silently ACCEPTED with truncated pw_shell/pw_dir/pw_gecos, because a
 * partial line ending at the buffer's own NUL terminator looks exactly
 * like a real last-line-with-no-trailing-newline to the old code). Fixed
 * by threading real truncation state out to the caller instead of
 * assuming the parser would catch it. */
static long li_load_file(const char *path, char *buf, li_size_t bufsize, int *truncated) {
    *truncated = 0;
    long fd = li_sys_open(path, LI_O_RDONLY, 0);
    if (fd < 0) {
        return fd;
    }
    li_size_t total = 0;
    for (;;) {
        if (total + 1 >= bufsize) {
            *truncated = 1; /* buffer full - unknown whether more file remains unread */
            break;
        }
        long r = li_sys_read((int)fd, buf + total, bufsize - 1 - total);
        if (r < 0) {
            li_sys_close((int)fd);
            return r;
        }
        if (r == 0) {
            break; /* real EOF - buf holds the complete file, not truncated */
        }
        total += (li_size_t)r;
    }
    li_sys_close((int)fd);
    buf[total] = 0;
    return (long)total;
}

/* li_pw_load()/li_gr_load()'s shared per-line loop shape (each still
 * keeps its own copy below, calling its own parse_one -- deliberately not
 * unified behind a function pointer: this is freestanding C with no
 * portable way to cast between differently-typed parse-function pointers
 * without real UB risk, and two short, direct copies is a fine trade for
 * avoiding that). Real fixes applied to both copies (adversarial review,
 * 2026-08-14):
 *  - the entry count is reset to 0 unconditionally, before attempting the
 *    load, not only on the success path (P1: the old code only reset the
 *    count when the load succeeded, so a *failed* reload left the count
 *    and the entire previous table untouched, silently continuing to
 *    serve stale data - or, worse, if the failure happened mid-read after
 *    partially overwriting the static file buffer with new bytes,
 *    entries built from a mix of old and new buffer content with no
 *    reliable NUL boundaries between them).
 *  - a line with no trailing '\n' is only accepted as real, complete data
 *    if the file was NOT truncated (real EOF, genuinely the last line) --
 *    if truncated, it's discarded rather than parsed with wrong/cut-off
 *    field data (P2, see li_load_file's corrected doc comment above).
 *  - a trailing '\r' (CRLF line endings) is trimmed from each line before
 *    parsing, so a CRLF-formatted file doesn't silently poison the last
 *    field (e.g. shell becoming "/bin/sh\r") on every single line. */

/* -------------------------------- passwd table --------------------------- */

static struct li_passwd li_pw_table[LI_MAX_PASSWD_ENTRIES];
static int li_pw_count;

/* Faithful adaptation of FreeBSD's real __pw_scan() (lib/libc/gen/
 * pw_scan.c, fetched fresh from the real upstream source as this file's
 * reference, not vendored/guessed) for the _PWSCAN_MASTER (10-field
 * master.passwd) case specifically -- this module only ever reads
 * /etc/master.passwd, never the derived 7-field /etc/passwd, so the
 * simpler non-master branch is intentionally not implemented here.
 * Real field order, confirmed directly against the fetched reference:
 *   name:passwd:uid:gid:class:change:expire:gecos:dir:shell
 * Returns 1 on success (matching real __pw_scan()'s return convention),
 * 0 on a malformed line (wrong field count, bad uid/gid) -- caller skips
 * the line, doesn't abort the whole file, matching pwd_mkdb/getpwent's
 * real tolerance for one bad line among many. */
static int li_pw_parse_line(char *line, struct li_passwd *pw) {
    char *bp = line;
    char *p;
    int ok;

    /* Empty name rejected (P3, adversarial review 2026-08-14): the
     * fetched real pw_scan.c reference actually *tolerates* an empty
     * name structurally (it only gates whether the _PWF_NAME bit gets
     * set, a flag this module doesn't use at all) - that tolerance in
     * the real code exists to support the historical '+'/'-' NIS-
     * include-line convention, which this module deliberately does not
     * implement (see li_gr_parse_line's matching comment below). Without
     * that convention, an empty name serves no purpose here and is a
     * real liability: a stray leading colon would silently create a
     * nameless, uid-0-if-otherwise-unset entry, and li_getpwnam("")
     * would match it. Rejected explicitly, mirroring the group parser's
     * existing (and, per the real getgrent.c reference, unconditional)
     * empty-gr_name rejection below - a deliberate deviation from the
     * letter of the passwd reference, for a documented reason. */
    if (!(pw->pw_name = li_strsep(&bp, ':')) || pw->pw_name[0] == 0) return 0;
    if (!(pw->pw_passwd = li_strsep(&bp, ':'))) return 0;

    if (!(p = li_strsep(&bp, ':'))) return 0;
    pw->pw_uid = (li_uid_t)li_strtoul10(p, &ok);
    if (!ok) return 0;

    if (!(p = li_strsep(&bp, ':'))) return 0;
    pw->pw_gid = (li_gid_t)li_strtoul10(p, &ok);
    if (!ok) return 0;

    if (!(pw->pw_class = li_strsep(&bp, ':'))) return 0;

    /* P4 (adversarial review 2026-08-14): the real reference's
     * `pw->pw_change = atol(p)` is unconditional and atol() has no error
     * signal at all, so real pw_scan.c silently coerces a garbled (but
     * non-empty) change/expire field to 0 rather than rejecting the
     * line. This module already deviates from atol()'s total
     * permissiveness for uid/gid (real security-relevant fields) above,
     * so for consistency a non-empty-but-non-numeric change/expire field
     * is treated as a real parse error here too, not silently coerced --
     * only a genuinely EMPTY field (a real, valid "no value set" case,
     * matching _PWF_CHANGE/_PWF_EXPIRE's own real semantics) defaults to
     * 0 without going through li_strtoul10 at all. */
    if (!(p = li_strsep(&bp, ':'))) return 0;
    if (p[0]) {
        pw->pw_change = (li_time_t)li_strtoul10(p, &ok);
        if (!ok) return 0;
    } else {
        pw->pw_change = 0;
    }

    if (!(p = li_strsep(&bp, ':'))) return 0;
    if (p[0]) {
        pw->pw_expire = (li_time_t)li_strtoul10(p, &ok);
        if (!ok) return 0;
    } else {
        pw->pw_expire = 0;
    }

    if (!(pw->pw_gecos = li_strsep(&bp, ':'))) return 0;
    if (!(pw->pw_dir = li_strsep(&bp, ':'))) return 0;
    if (!(pw->pw_shell = li_strsep(&bp, ':'))) return 0;

    /* Real __pw_scan() rejects a line with extra trailing fields
     * ("too many" -> corrupted entry, see the fetched pw_scan.c's own
     * `if ((p = strsep(&bp, ":"))) { fmt: ... return 0; }` tail check) --
     * matched here for the same reason: a master.passwd line with an
     * unexpected 11th field is far more likely a real parsing
     * desync (e.g. an unescaped colon in gecos) than a benign extra
     * field, and silently accepting it risks mis-assigning which text
     * ended up in pw_shell. */
    if (li_strsep(&bp, ':') != 0) return 0;

    return 1;
}

int li_pw_load(const char *path) {
    li_pw_count = 0; /* P1: reset unconditionally, before the load can fail -
                       * a failed reload must not leave a stale (or, if the
                       * failure was mid-read, possibly corrupted) previous
                       * table being served. */
    int truncated = 0;
    long n = li_load_file(path, li_pw_filebuf, LI_FILE_BUF_SIZE, &truncated);
    if (n < 0) {
        return -1;
    }
    char *p = li_pw_filebuf;
    while (*p && li_pw_count < LI_MAX_PASSWD_ENTRIES) {
        char *line_end = p;
        while (*line_end && *line_end != '\n') line_end++;
        int had_nl = (*line_end == '\n');
        if (!had_nl && truncated) {
            break; /* P2: final, genuinely-incomplete line - discard, don't guess */
        }
        if (had_nl) {
            *line_end = 0;
        }
        if (line_end > p && line_end[-1] == '\r') {
            line_end[-1] = 0; /* trim CRLF's trailing \r */
        }
        if (*p != 0) { /* skip blank lines */
            if (li_pw_parse_line(p, &li_pw_table[li_pw_count])) {
                li_pw_count++;
            }
        }
        p = had_nl ? line_end + 1 : line_end;
    }
    return li_pw_count;
}

struct li_passwd *li_getpwnam(const char *name) {
    for (int i = 0; i < li_pw_count; i++) {
        if (li_streq(li_pw_table[i].pw_name, name)) {
            return &li_pw_table[i];
        }
    }
    return 0;
}

struct li_passwd *li_getpwuid(li_uid_t uid) {
    for (int i = 0; i < li_pw_count; i++) {
        if (li_pw_table[i].pw_uid == uid) {
            return &li_pw_table[i];
        }
    }
    return 0;
}

/* -------------------------------- group table ----------------------------- */

static struct li_group li_gr_table[LI_MAX_GROUP_ENTRIES];
static int li_gr_count;

/* Faithful adaptation of FreeBSD's real __gr_parse_entry() (lib/libc/gen/
 * getgrent.c, same fetched-fresh reference discipline as the passwd
 * parser above). Real field order:
 *   name:passwd:gid:member1,member2,...
 * Empty gr_name or empty gid field is a real reject (matches the fetched
 * reference's own `grp->gr_name[0] == '\0'` / `s_gid[0] == '\0'` checks --
 * unlike passwd's uid/gid, which reference source treats an EMPTY name/gid
 * specially only for the +/- NIS-include-line convention this module
 * doesn't implement, group's real code has no such exception). */
static int li_gr_parse_line(char *line, struct li_group *gr) {
    char *bp = line;
    char *p;
    int ok;

    if (!(gr->gr_name = li_strsep(&bp, ':')) || gr->gr_name[0] == 0) return 0;
    if (!(gr->gr_passwd = li_strsep(&bp, ':'))) return 0;

    if (!(p = li_strsep(&bp, ':')) || p[0] == 0) return 0;
    gr->gr_gid = (li_gid_t)li_strtoul10(p, &ok);
    if (!ok) return 0;

    gr->gr_nmem = 0;
    char *members = bp; /* remainder of the line, comma-separated */
    while (members && *members && gr->gr_nmem < LI_GROUP_MAX_MEMBERS) {
        char *m = li_strsep(&members, ',');
        if (m && m[0]) {
            gr->gr_mem[gr->gr_nmem++] = m;
        }
    }
    gr->gr_mem[gr->gr_nmem] = 0; /* NULL-terminated too, real-struct-group-compatible iteration */
    return 1;
}

int li_gr_load(const char *path) {
    li_gr_count = 0; /* P1: see li_pw_load's matching comment */
    int truncated = 0;
    long n = li_load_file(path, li_gr_filebuf, LI_FILE_BUF_SIZE, &truncated);
    if (n < 0) {
        return -1;
    }
    char *p = li_gr_filebuf;
    while (*p && li_gr_count < LI_MAX_GROUP_ENTRIES) {
        char *line_end = p;
        while (*line_end && *line_end != '\n') line_end++;
        int had_nl = (*line_end == '\n');
        if (!had_nl && truncated) {
            break; /* P2: see li_pw_load's matching comment */
        }
        if (had_nl) {
            *line_end = 0;
        }
        if (line_end > p && line_end[-1] == '\r') {
            line_end[-1] = 0; /* trim CRLF's trailing \r */
        }
        if (*p != 0) {
            if (li_gr_parse_line(p, &li_gr_table[li_gr_count])) {
                li_gr_count++;
            }
        }
        p = had_nl ? line_end + 1 : line_end;
    }
    return li_gr_count;
}

struct li_group *li_getgrnam(const char *name) {
    for (int i = 0; i < li_gr_count; i++) {
        if (li_streq(li_gr_table[i].gr_name, name)) {
            return &li_gr_table[i];
        }
    }
    return 0;
}

struct li_group *li_getgrgid(li_gid_t gid) {
    for (int i = 0; i < li_gr_count; i++) {
        if (li_gr_table[i].gr_gid == gid) {
            return &li_gr_table[i];
        }
    }
    return 0;
}
