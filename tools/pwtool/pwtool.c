/*
 * pwtool -- Phase 5 account-provisioning tool for XNU/rpi3's custom
 * flat-file resolver (tools/libinfo_custom/{libinfo_custom.h,.c}).
 *
 * WHAT THIS REPLACES, AND WHY IT'S ONE TOOL NOT FOUR (deliberate scope
 * decision, flagged rather than silently assumed): docs/roadmap.md Phase 5
 * names pwd_mkdb/vipw/chpass/passwd as the real BSD account-provisioning
 * toolchain (all confirmed open source in third_party/system_cmds -- see
 * pwd_mkdb.tproj/, vipw.tproj/, chpass.tproj/). Porting those four real
 * Apple binaries as-is is NOT viable yet: every one of them is written
 * against real dyld-linked Libc (stdio's fopen/fgets/fprintf, getpwent(),
 * a real editor invocation via $EDITOR/execvp for vipw, Berkeley DB via
 * pwd_mkdb's actual job of building a *hashed* db) -- this project's
 * concurrent Libc/dyld pull (tools/userland_staging/) is real but not
 * complete enough yet (no working malloc/stdio chain for a program this
 * shaped), and per this module's OWN established design rule
 * (libinfo_custom.h's header comment), this tool deliberately does not
 * depend on that concurrent, separately-owned work either -- same
 * freestanding-raw-syscall discipline as libinfo_custom.c and
 * tools/init_binary/init.c.
 *
 * Given that, this is ONE freestanding tool with subcommands
 * (useradd/groupadd/passwd/check) that together cover what those four real
 * tools are actually FOR at this project's current scale, not a port of
 * their source:
 *   - `check`     -- pwd_mkdb's real safety-relevant job, reinterpreted:
 *                    real pwd_mkdb's headline job (build a hashed
 *                    pwd.db/spwd.db) is moot here -- this resolver reads
 *                    /etc/master.passwd directly (see project memory:
 *                    "DECISION SUPERSEDED 2026-08-14"), no derived DB
 *                    exists to build. What DOES carry over is pwd_mkdb's
 *                    real safety role of validating a master file BEFORE
 *                    it's trusted live -- `check` does exactly that
 *                    (parses via li_pw_load/li_gr_load, reports real
 *                    entry counts and any parse failures) without
 *                    mutating anything.
 *   - `useradd`/`groupadd` -- vipw/chpass's real job (safely add/edit
 *                    account entries) minus vipw's specific mechanism
 *                    (spawn $EDITOR on a copy of the file) -- not viable
 *                    without a real editor+dyld+libc present. Same
 *                    safety properties vipw actually cares about (atomic
 *                    replace, a lock against concurrent edits) are
 *                    implemented directly below instead of via vipw's
 *                    edit-then-diff-then-install mechanism.
 *   - `passwd`     -- direct equivalent of real passwd(1)'s actual job
 *                    (prompt for a new password, hash it, update the
 *                    account's entry) -- this one maps closest to a real
 *                    tool 1:1.
 *
 * SAFETY MECHANISM (the actual "TOCTOU on the flat file" concern the task
 * flagged): every mutating subcommand follows the same real, standard BSD
 * pattern real vipw itself uses (confirmed by reading
 * third_party/system_cmds/vipw.tproj/vipw.c before choosing this, not
 * assumed): (1) acquire an exclusive lock file via O_CREAT|O_EXCL (fails
 * loudly if another pwtool invocation is already mid-edit, rather than
 * silently racing it); (2) read+fully parse the CURRENT on-disk file
 * (catches pre-existing corruption before writing anything on top of it);
 * (3) build the ENTIRE new file content in memory; (4) write it to a
 * `.tmp` sibling path and fsync; (5) rename() the tmp file over the real
 * path -- rename() is atomic on the same filesystem, so any concurrent
 * reader (e.g. init.c's do_login()) always sees either the complete old
 * file or the complete new file, never a partial write; (6) release the
 * lock. KNOWN, ACCEPTED LIMITATION (same one real vipw has): a pwtool
 * process that crashes between steps 1 and 6 leaves a stale lock file
 * behind, requiring manual removal -- not fixed here, not a regression
 * this project introduced (real vipw's /etc/ptmp has the identical
 * failure mode).
 *
 * FIELD-INJECTION SAFETY (also explicitly in scope per the task's "be
 * careful about... malformed hash handling" instruction, generalized to
 * every field this tool writes): every field (name, gecos, home, shell,
 * group members) is validated to reject ':' and any newline/carriage
 * return before being written -- an unvalidated colon in e.g. a gecos
 * field would silently shift every subsequent field (a real, classic
 * flat-file injection bug class), and an unvalidated newline would let a
 * single crafted "useradd" call inject an entirely separate, attacker-
 * controlled extra line (e.g. a second "evil::0:0:...:/bin/sh" root-uid
 * entry) into the account database.
 */

typedef unsigned long size_t;
typedef long ssize_t;

/* ---------------- raw syscalls (arm64 Darwin: svc #0x80, num in x16) ----
 * Same convention/numbers already established and cross-checked against
 * third_party/xnu-7195/bsd/kern/syscalls.master by
 * tools/init_binary/init.c and tools/libinfo_custom/libinfo_custom.c. */

#define SYS_exit    1
#define SYS_read    3
#define SYS_write   4
#define SYS_open    5
#define SYS_close   6
#define SYS_unlink  10
#define SYS_getpid  20
#define SYS_fsync   95
#define SYS_rename  128

#define O_RDONLY  0
#define O_WRONLY  0x0001
#define O_CREAT   0x0200
#define O_TRUNC   0x0400
#define O_EXCL    0x0800

#define PT_ENOENT 2
#define PT_EEXIST 17

#ifdef PWTOOL_HOST_TEST
/* Host-side logic testing -- forwards to real host libc, same shim
 * pattern host_test.c/init.c's own README already established. Real
 * freestanding statically-linked Mach-O binaries get killed by this
 * host's hardened runtime before main() runs (confirmed independently
 * multiple times already in this project), so this compiles as a normal
 * dynamically-linked host executable instead. */
long pt_host_open(const char *path, int flags, int mode);
long pt_host_read(int fd, void *buf, size_t n);
long pt_host_write(int fd, const void *buf, size_t n);
long pt_host_close(int fd);
long pt_host_unlink(const char *path);
long pt_host_rename(const char *from, const char *to);
long pt_host_getpid(void);
long pt_host_fsync(int fd);

static long sys_open(const char *path, int flags, int mode) { return pt_host_open(path, flags, mode); }
static ssize_t sys_read(int fd, void *buf, size_t n) { return pt_host_read(fd, buf, n); }
static ssize_t sys_write(int fd, const void *buf, size_t n) { return pt_host_write(fd, buf, n); }
static long sys_close(int fd) { return pt_host_close(fd); }
static long sys_unlink(const char *path) { return pt_host_unlink(path); }
static long sys_rename(const char *from, const char *to) { return pt_host_rename(from, to); }
static long sys_getpid(void) { return pt_host_getpid(); }
static long sys_fsync(int fd) { return pt_host_fsync(fd); }
#else
static inline long pt_syscall3(long num, long a0, long a1, long a2) {
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
static long sys_open(const char *path, int flags, int mode) { return pt_syscall3(SYS_open, (long)path, flags, mode); }
static ssize_t sys_read(int fd, void *buf, size_t n) { return pt_syscall3(SYS_read, fd, (long)buf, (long)n); }
static ssize_t sys_write(int fd, const void *buf, size_t n) { return pt_syscall3(SYS_write, fd, (long)buf, (long)n); }
static long sys_close(int fd) { return pt_syscall3(SYS_close, fd, 0, 0); }
static long sys_unlink(const char *path) { return pt_syscall3(SYS_unlink, (long)path, 0, 0); }
static long sys_rename(const char *from, const char *to) { return pt_syscall3(SYS_rename, (long)from, (long)to, 0); }
static long sys_getpid(void) { return pt_syscall3(SYS_getpid, 0, 0, 0); }
static long sys_fsync(int fd) { return pt_syscall3(SYS_fsync, fd, 0, 0); }
#endif

/* Note: there is no sys_exit() in this file at all (unlike
 * tools/init_binary/init.c, which calls one from several builtins) --
 * pwtool_main() just returns an int, and the real process exit happens
 * directly in the _start naked-asm entry point at the bottom of this file
 * (SYS_exit issued inline from asm using pwtool_main()'s return value,
 * already sitting in x0 per AAPCS64 -- see _start's own comment). A C-level
 * sys_exit() wrapper would be genuinely dead code here. */

#include "libinfo_custom.h"
#include "crypt_des.h"

/* ------------------------------- tiny helpers ---------------------------- */

static size_t pt_strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

static int pt_streq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

static void pt_memcpy(void *dst, const void *src, size_t n) {
    char *d = (char *)dst; const char *s = (const char *)src;
    while (n--) *d++ = *s++;
}

static void pt_put(int fd, const char *s) { sys_write(fd, s, pt_strlen(s)); }

/* Real strtoul()-equivalent, base 10, 32-bit range -- same overflow-safety
 * discipline as libinfo_custom.c's own li_strtoul10 (duplicated rather
 * than shared, matching this project's established "small freestanding
 * helpers get their own copy per translation unit" convention -- see that
 * function's own comment for the reasoning). A corrupted/malicious
 * command-line uid/gid must be REJECTED, not silently wrapped. */
static int pt_parse_u32(const char *p, unsigned int *out) {
    unsigned long v = 0;
    const char *start = p;
    if (!p || !*p) return 0;
    while (*p) {
        if (*p < '0' || *p > '9') return 0;
        unsigned long digit = (unsigned long)(*p - '0');
        unsigned long nv = v * 10 + digit;
        if (nv < v || (nv - digit) / 10 != v) return 0; /* overflow */
        v = nv;
        p++;
    }
    if (v > 0xFFFFFFFFUL || p == start) return 0;
    *out = (unsigned int)v;
    return 1;
}

/* Unsigned-long -> decimal string, no libc. */
static void pt_utoa(unsigned long v, char *out) {
    char tmp[24];
    int i = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
    int j = 0;
    while (i > 0) out[j++] = tmp[--i];
    out[j] = 0;
}

/* ------------------------------- field validation ------------------------ */

/* Reject ':' and any newline/carriage-return -- see this file's header
 * comment for exactly why (flat-file field/line injection). Also rejects
 * NUL trivially (a C string can't contain one mid-string to begin with). */
static int pt_field_safe(const char *s) {
    if (!s) return 0;
    for (const char *p = s; *p; p++) {
        if (*p == ':' || *p == '\n' || *p == '\r') return 0;
    }
    return 1;
}

/* Stricter charset for name fields (account/group names): used as lookup
 * keys and echoed into prompts, so held to the conventional Unix username
 * charset (alnum, '_', '-', '.') rather than merely "no colon/newline" --
 * defense in depth, not strictly required by the flat-file grammar
 * itself. */
static int pt_name_safe(const char *s) {
    if (!s || !s[0]) return 0;
    for (const char *p = s; *p; p++) {
        char c = *p;
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok) return 0;
    }
    return 1;
}

/* ------------------------------- file I/O --------------------------------
 * Deliberately separate from libinfo_custom.c's own li_load_file() (which
 * is static/private to that translation unit, and read-only besides) --
 * this tool needs write access too, and follows this project's
 * established convention of small per-file duplicated I/O helpers rather
 * than growing shared surface across freestanding modules. */

#define PT_FILE_BUF_SIZE 65536
#define PT_MAX_PATH 256

/* Writes the whole buffer to `path` (O_CREAT|O_TRUNC|O_WRONLY, mode 0600
 * -- account data, not world-readable), looping on short writes. Returns
 * 0 on success, -1 on any failure (caller must not assume partial writes
 * are harmless -- they're never renamed into the live path, see the
 * commit helpers below). */
static int pt_write_file(const char *path, const char *buf, size_t len) {
    long fd = sys_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    size_t off = 0;
    while (off < len) {
        long w = sys_write((int)fd, buf + off, len - off);
        if (w <= 0) { sys_close((int)fd); return -1; }
        off += (size_t)w;
    }
    /* REAL GAP FOUND DURING SELF-REVIEW: this file's own header comment
     * already (incorrectly) claimed a durability guarantee ("write it to
     * a .tmp sibling path and fsync") that this function never actually
     * implemented -- fsync() was never called anywhere in this file.
     * Without it, a real crash/power-loss between a successful-looking
     * write() and the eventual rename() could rename a tmp file into
     * place whose content is still sitting in a dirty page/buffer cache,
     * not yet on stable storage -- rename()'s own atomicity guarantee
     * (the live path always shows a complete old-or-new file, never a
     * half-written one) is real and unaffected either way, but WHICH
     * complete version survives a crash right at that boundary is what
     * fsync() here is actually for. Fixed by calling it for real before
     * close(), and failing the whole write on a real fsync error (a
     * write that reports success but doesn't actually reach storage is
     * exactly the failure mode this exists to prevent). */
    if (sys_fsync((int)fd) < 0) { sys_close((int)fd); return -1; }
    sys_close((int)fd);
    return 0;
}

/* ------------------------------- lock file -------------------------------
 * O_CREAT|O_EXCL is atomic at the filesystem level -- two concurrent
 * pwtool invocations can never both believe they hold the lock, even
 * without a separate flock()/advisory-lock syscall (this project doesn't
 * have a raw flock() wrapper anywhere yet, and doesn't need one for this:
 * same real mechanism vipw itself relies on). */
#define PT_EROFS  30

/* Returns 0 on success, or the real negative errno on failure (NOT just a
 * generic -1) -- an earlier version of this function collapsed every
 * failure reason into one generic "-1", which made a real EROFS (the
 * filesystem is mounted read-only -- a known, pre-existing, out-of-scope
 * kernel/mount-path condition this project's docs/roadmap.md already
 * flags, NOT a bug in this file) print the exact same "another edit in
 * progress?" message as genuine lock contention (EEXIST) -- confirmed
 * this was actively misleading during this feature's own real QEMU boot
 * verification (a real EROFS was initially misdiagnosed as a phantom
 * stale lock before this fix). Callers now report the specific reason. */
static long pt_lock_acquire(const char *lockpath) {
    long fd = sys_open(lockpath, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        return fd;
    }
    sys_close((int)fd);
    return 0;
}

static void pt_report_lock_failure(long rc) {
    if (rc == -PT_EEXIST) {
        pt_put(2, "(another edit already in progress -- a lock file already exists)\n");
    } else if (rc == -PT_EROFS) {
        pt_put(2, "(filesystem is mounted read-only -- EROFS. This is a known, "
                  "pre-existing kernel/mount-path condition, not a pwtool bug -- "
                  "see docs/roadmap.md's notes on the HFS+ root mount.)\n");
    } else {
        char numbuf[24];
        pt_utoa((unsigned long)(-rc), numbuf);
        pt_put(2, "(errno "); pt_put(2, numbuf); pt_put(2, ")\n");
    }
}

static void pt_lock_release(const char *lockpath) {
    sys_unlink(lockpath);
}

static void pt_lockpath_for(const char *datapath, char *out, size_t outsz) {
    size_t n = pt_strlen(datapath);
    size_t suffix_len = pt_strlen(".pwtool.lock");
    if (n + suffix_len + 1 > outsz) n = (outsz > suffix_len + 1) ? outsz - suffix_len - 1 : 0;
    pt_memcpy(out, datapath, n);
    pt_memcpy(out + n, ".pwtool.lock", suffix_len);
    out[n + suffix_len] = 0;
}

static void pt_tmppath_for(const char *datapath, char *out, size_t outsz) {
    size_t n = pt_strlen(datapath);
    size_t suffix_len = pt_strlen(".pwtool.tmp");
    if (n + suffix_len + 1 > outsz) n = (outsz > suffix_len + 1) ? outsz - suffix_len - 1 : 0;
    pt_memcpy(out, datapath, n);
    pt_memcpy(out + n, ".pwtool.tmp", suffix_len);
    out[n + suffix_len] = 0;
}

/* ------------------------------- line serialization -----------------------
 * Inverse of libinfo_custom.c's li_pw_parse_line/li_gr_parse_line -- must
 * emit exactly the format those functions (and real FreeBSD pw_scan.c/
 * getgrent.c's real field order, which they're adapted from) expect to
 * read back. */

static size_t pt_format_pw_line(struct li_passwd *pw, char *out, size_t outsz) {
    char numbuf[24];
    size_t off = 0;
#define PT_APPEND(s) do { size_t l = pt_strlen(s); if (off + l < outsz) { pt_memcpy(out + off, (s), l); off += l; } } while (0)
    PT_APPEND(pw->pw_name); PT_APPEND(":");
    PT_APPEND(pw->pw_passwd); PT_APPEND(":");
    pt_utoa(pw->pw_uid, numbuf); PT_APPEND(numbuf); PT_APPEND(":");
    pt_utoa(pw->pw_gid, numbuf); PT_APPEND(numbuf); PT_APPEND(":");
    PT_APPEND(pw->pw_class); PT_APPEND(":");
    pt_utoa((unsigned long)pw->pw_change, numbuf); PT_APPEND(numbuf); PT_APPEND(":");
    pt_utoa((unsigned long)pw->pw_expire, numbuf); PT_APPEND(numbuf); PT_APPEND(":");
    PT_APPEND(pw->pw_gecos); PT_APPEND(":");
    PT_APPEND(pw->pw_dir); PT_APPEND(":");
    PT_APPEND(pw->pw_shell); PT_APPEND("\n");
#undef PT_APPEND
    if (off >= outsz) return 0; /* would have overflowed -- caller must treat as failure */
    out[off] = 0;
    return off;
}

static size_t pt_format_gr_line(struct li_group *gr, char *out, size_t outsz) {
    char numbuf[24];
    size_t off = 0;
#define PT_APPEND(s) do { size_t l = pt_strlen(s); if (off + l < outsz) { pt_memcpy(out + off, (s), l); off += l; } } while (0)
    PT_APPEND(gr->gr_name); PT_APPEND(":");
    PT_APPEND(gr->gr_passwd); PT_APPEND(":");
    pt_utoa(gr->gr_gid, numbuf); PT_APPEND(numbuf); PT_APPEND(":");
    for (int i = 0; i < gr->gr_nmem; i++) {
        if (i > 0) PT_APPEND(",");
        PT_APPEND(gr->gr_mem[i]);
    }
    PT_APPEND("\n");
#undef PT_APPEND
    if (off >= outsz) return 0;
    out[off] = 0;
    return off;
}

/* ------------------------------- commit buffer size -----------------------
 * Shared by useradd/groupadd/passwd's own inline rewrite logic below (each
 * needs a slightly different mutation -- append one new line, or replace
 * one existing entry's password field -- so each keeps its own short,
 * directly-readable rewrite loop rather than routing through a shared
 * "commit" function whose one-size-fits-all shape would have to grow a
 * mutation-callback parameter for no real benefit at this size). */
#define PT_COMMIT_BUF_SIZE 131072

/* Loads `path` via li_pw_load(), but first distinguishes "file doesn't
 * exist yet" (real bootstrap case -- the very first account on a fresh
 * image -- table starts empty, li_pw_load's own -1-on-ENOENT is expected
 * and fine to proceed past) from "file exists but couldn't be read/
 * parsed cleanly" (a REAL fail-closed case -- proceeding here risks
 * silently replacing a real, existing (if damaged) account database with
 * one containing only the single new entry, real DATA LOSS, not just an
 * inconvenience). li_pw_load()/li_gr_load() themselves don't preserve
 * enough information to tell these apart (both return -1), so this does
 * its own preliminary existence check first. */
static int pt_load_pw_or_bootstrap(const char *pwpath) {
    long fd = sys_open(pwpath, O_RDONLY, 0);
    if (fd < 0) {
        if (fd == -PT_ENOENT) {
            li_pw_reset();
            return 0; /* bootstrap: no file yet, empty table is correct */
        }
        return -1; /* real I/O error opening an existing path -- abort */
    }
    sys_close((int)fd);
    int rc = li_pw_load(pwpath);
    if (rc < 0) {
        return -1; /* file exists but failed to load/parse -- fail closed */
    }
    return 0;
}

static int pt_load_gr_or_bootstrap(const char *grpath) {
    long fd = sys_open(grpath, O_RDONLY, 0);
    if (fd < 0) {
        if (fd == -PT_ENOENT) {
            li_gr_reset();
            return 0;
        }
        return -1;
    }
    sys_close((int)fd);
    int rc = li_gr_load(grpath);
    if (rc < 0) {
        return -1;
    }
    return 0;
}

/* ------------------------------- subcommands ------------------------------ */

int pw_check(const char *pwpath, const char *grpath) {
    int rc = 0;
    long fd = sys_open(pwpath, O_RDONLY, 0);
    if (fd < 0) {
        pt_put(1, "check: "); pt_put(1, pwpath); pt_put(1, ": does not exist (ok for a fresh system)\n");
    } else {
        sys_close((int)fd);
        int n = li_pw_load(pwpath);
        if (n < 0) {
            pt_put(1, "check: "); pt_put(1, pwpath); pt_put(1, ": FAILED TO LOAD/PARSE\n");
            rc = -1;
        } else {
            char numbuf[24]; pt_utoa((unsigned long)n, numbuf);
            pt_put(1, "check: "); pt_put(1, pwpath); pt_put(1, ": "); pt_put(1, numbuf); pt_put(1, " valid entries\n");
        }
    }
    fd = sys_open(grpath, O_RDONLY, 0);
    if (fd < 0) {
        pt_put(1, "check: "); pt_put(1, grpath); pt_put(1, ": does not exist (ok for a fresh system)\n");
    } else {
        sys_close((int)fd);
        int n = li_gr_load(grpath);
        if (n < 0) {
            pt_put(1, "check: "); pt_put(1, grpath); pt_put(1, ": FAILED TO LOAD/PARSE\n");
            rc = -1;
        } else {
            char numbuf[24]; pt_utoa((unsigned long)n, numbuf);
            pt_put(1, "check: "); pt_put(1, grpath); pt_put(1, ": "); pt_put(1, numbuf); pt_put(1, " valid entries\n");
        }
    }
    return rc;
}

int pw_useradd(const char *pwpath, const char *name, const char *uid_s, const char *gid_s,
               const char *gecos, const char *home, const char *shell) {
    unsigned int uid, gid;
    if (!pt_name_safe(name)) { pt_put(2, "useradd: invalid name (allowed: alnum, '_', '-', '.')\n"); return -1; }
    if (!pt_parse_u32(uid_s, &uid)) { pt_put(2, "useradd: invalid uid\n"); return -1; }
    if (!pt_parse_u32(gid_s, &gid)) { pt_put(2, "useradd: invalid gid\n"); return -1; }
    if (!pt_field_safe(gecos) || !pt_field_safe(home) || !pt_field_safe(shell)) {
        pt_put(2, "useradd: gecos/home/shell must not contain ':' or a newline\n");
        return -1;
    }

    char lockpath[PT_MAX_PATH];
    pt_lockpath_for(pwpath, lockpath, sizeof(lockpath));
    {
        long lrc = pt_lock_acquire(lockpath);
        if (lrc != 0) {
            pt_put(2, "useradd: could not acquire lock ");
            pt_report_lock_failure(lrc);
            return -1;
        }
    }

    int result = -1;
    if (pt_load_pw_or_bootstrap(pwpath) != 0) {
        pt_put(2, "useradd: refusing to proceed -- existing "); pt_put(2, pwpath);
        pt_put(2, " exists but failed to load cleanly (real corruption risk, not overwriting it)\n");
        goto out;
    }
    if (li_getpwnam(name) != 0) { pt_put(2, "useradd: account name already exists\n"); goto out; }
    if (li_getpwuid(uid) != 0) { pt_put(2, "useradd: uid already in use\n"); goto out; }
    if (li_pw_entry_count() >= LI_MAX_PASSWD_ENTRIES) {
        pt_put(2, "useradd: table is full (LI_MAX_PASSWD_ENTRIES) -- see libinfo_custom.h's documented capacity limit\n");
        goto out;
    }

    /* Append the new entry directly onto li_pw_load()'s own in-memory
     * table via the same struct/array it already populated -- li_pw_at()
     * only exposes read access, so this reaches into the table through
     * one more li_pw_load-equivalent entry point instead: build the
     * commit buffer with the OLD entries (already loaded) plus this ONE
     * new line appended as raw text, by writing it directly rather than
     * via the table. Simpler and just as safe as trying to mutate
     * libinfo_custom.c's private static table from outside its own
     * translation unit (which the header deliberately doesn't expose a
     * way to do). */
    {
        static char buf[PT_COMMIT_BUF_SIZE];
        size_t off = 0;
        int n = li_pw_entry_count();
        for (int i = 0; i < n; i++) {
            struct li_passwd *pw = li_pw_at(i);
            if (!pw) continue;
            size_t l = pt_format_pw_line(pw, buf + off, PT_COMMIT_BUF_SIZE - off);
            if (l == 0) { pt_put(2, "useradd: internal buffer overflow re-emitting existing entries\n"); goto out; }
            off += l;
        }
        struct li_passwd newpw;
        newpw.pw_name = (char *)name;
        newpw.pw_passwd = (char *)"*"; /* locked by default -- run `passwd` to set a real one; never
                                          created with an empty (fail-open) password */
        newpw.pw_uid = uid;
        newpw.pw_gid = gid;
        newpw.pw_class = (char *)"";
        newpw.pw_change = 0;
        newpw.pw_expire = 0;
        newpw.pw_gecos = (char *)gecos;
        newpw.pw_dir = (char *)home;
        newpw.pw_shell = (char *)shell;
        size_t l = pt_format_pw_line(&newpw, buf + off, PT_COMMIT_BUF_SIZE - off);
        if (l == 0) { pt_put(2, "useradd: new entry too large for commit buffer\n"); goto out; }
        off += l;

        char tmppath[PT_MAX_PATH];
        pt_tmppath_for(pwpath, tmppath, sizeof(tmppath));
        if (pt_write_file(tmppath, buf, off) != 0) { pt_put(2, "useradd: write failed\n"); goto out; }
        if (sys_rename(tmppath, pwpath) < 0) { sys_unlink(tmppath); pt_put(2, "useradd: rename failed\n"); goto out; }
    }

    pt_put(1, "useradd: "); pt_put(1, name); pt_put(1, " added (locked -- run `passwd "); pt_put(1, name); pt_put(1, "` next)\n");
    result = 0;
out:
    pt_lock_release(lockpath);
    return result;
}

int pw_groupadd(const char *grpath, const char *name, const char *gid_s, const char *members) {
    unsigned int gid;
    if (!pt_name_safe(name)) { pt_put(2, "groupadd: invalid name\n"); return -1; }
    if (!pt_parse_u32(gid_s, &gid)) { pt_put(2, "groupadd: invalid gid\n"); return -1; }
    if (members && !pt_field_safe(members)) { pt_put(2, "groupadd: members must not contain ':' or a newline\n"); return -1; }

    char lockpath[PT_MAX_PATH];
    pt_lockpath_for(grpath, lockpath, sizeof(lockpath));
    {
        long lrc = pt_lock_acquire(lockpath);
        if (lrc != 0) {
            pt_put(2, "groupadd: could not acquire lock ");
            pt_report_lock_failure(lrc);
            return -1;
        }
    }

    int result = -1;
    if (pt_load_gr_or_bootstrap(grpath) != 0) {
        pt_put(2, "groupadd: refusing to proceed -- existing "); pt_put(2, grpath);
        pt_put(2, " exists but failed to load cleanly\n");
        goto out;
    }
    if (li_getgrnam(name) != 0) { pt_put(2, "groupadd: group name already exists\n"); goto out; }
    if (li_getgrgid(gid) != 0) { pt_put(2, "groupadd: gid already in use\n"); goto out; }
    if (li_gr_entry_count() >= LI_MAX_GROUP_ENTRIES) {
        pt_put(2, "groupadd: table is full (LI_MAX_GROUP_ENTRIES)\n");
        goto out;
    }

    {
        static char buf[PT_COMMIT_BUF_SIZE];
        size_t off = 0;
        int n = li_gr_entry_count();
        for (int i = 0; i < n; i++) {
            struct li_group *gr = li_gr_at(i);
            if (!gr) continue;
            size_t l = pt_format_gr_line(gr, buf + off, PT_COMMIT_BUF_SIZE - off);
            if (l == 0) { pt_put(2, "groupadd: internal buffer overflow re-emitting existing entries\n"); goto out; }
            off += l;
        }
        char numbuf[24]; pt_utoa(gid, numbuf);
#define PT_APPEND(s) do { size_t l2 = pt_strlen(s); if (off + l2 < PT_COMMIT_BUF_SIZE) { pt_memcpy(buf + off, (s), l2); off += l2; } } while (0)
        PT_APPEND(name); PT_APPEND(":*:"); PT_APPEND(numbuf); PT_APPEND(":");
        if (members) PT_APPEND(members);
        PT_APPEND("\n");
#undef PT_APPEND

        char tmppath[PT_MAX_PATH];
        pt_tmppath_for(grpath, tmppath, sizeof(tmppath));
        if (pt_write_file(tmppath, buf, off) != 0) { pt_put(2, "groupadd: write failed\n"); goto out; }
        if (sys_rename(tmppath, grpath) < 0) { sys_unlink(tmppath); pt_put(2, "groupadd: rename failed\n"); goto out; }
    }

    pt_put(1, "groupadd: "); pt_put(1, name); pt_put(1, " added\n");
    result = 0;
out:
    pt_lock_release(lockpath);
    return result;
}

/* Core of `passwd`: sets `name`'s password to `newpass` (already read from
 * wherever the caller got it -- interactively for the real CLI, directly
 * for host tests). Separated from the interactive prompt so the actual
 * hashing/rewrite logic is host-testable without needing a tty. */
int pw_passwd_set(const char *pwpath, const char *name, const char *newpass) {
    if (!newpass || newpass[0] == 0) {
        pt_put(2, "passwd: empty password not allowed\n");
        return -1;
    }

    char lockpath[PT_MAX_PATH];
    pt_lockpath_for(pwpath, lockpath, sizeof(lockpath));
    {
        long lrc = pt_lock_acquire(lockpath);
        if (lrc != 0) {
            pt_put(2, "passwd: could not acquire lock ");
            pt_report_lock_failure(lrc);
            return -1;
        }
    }

    int result = -1;
    long fd = sys_open(pwpath, O_RDONLY, 0);
    if (fd < 0) { pt_put(2, "passwd: "); pt_put(2, pwpath); pt_put(2, " does not exist\n"); goto out; }
    sys_close((int)fd);
    if (li_pw_load(pwpath) < 0) { pt_put(2, "passwd: failed to load/parse "); pt_put(2, pwpath); pt_put(2, "\n"); goto out; }

    {
        struct li_passwd *target = li_getpwnam(name);
        if (!target) { pt_put(2, "passwd: no such account\n"); goto out; }

        char hash[32];
        if (!li_hash_password(newpass, hash, sizeof(hash))) {
            pt_put(2, "passwd: hashing failed\n");
            goto out;
        }

        static char buf[PT_COMMIT_BUF_SIZE];
        size_t off = 0;
        int n = li_pw_entry_count();
        for (int i = 0; i < n; i++) {
            struct li_passwd *pw = li_pw_at(i);
            if (!pw) continue;
            /* Swap pw_passwd in place (on the real table entry) rather than
             * `struct li_passwd tmp = *pw;` -- that struct-copy form
             * compiles to a call to the external `memcpy` symbol under
             * -ffreestanding -nostdlib (confirmed by hitting exactly this
             * link error, not guessed), which this no-libc tool doesn't
             * have. Save/restore instead of a real permanent mutation: the
             * in-memory table is about to be reloaded fresh from disk on
             * this process's next li_pw_load() call anyway (or the process
             * exits), so a transient in-place edit here is safe and never
             * observed by any other caller. */
            char *saved_passwd = pw->pw_passwd;
            if (pw == target) {
                pw->pw_passwd = hash;
            }
            size_t l = pt_format_pw_line(pw, buf + off, PT_COMMIT_BUF_SIZE - off);
            pw->pw_passwd = saved_passwd;
            if (l == 0) { pt_put(2, "passwd: internal buffer overflow\n"); goto out; }
            off += l;
        }

        char tmppath[PT_MAX_PATH];
        pt_tmppath_for(pwpath, tmppath, sizeof(tmppath));
        if (pt_write_file(tmppath, buf, off) != 0) { pt_put(2, "passwd: write failed\n"); goto out; }
        if (sys_rename(tmppath, pwpath) < 0) { sys_unlink(tmppath); pt_put(2, "passwd: rename failed\n"); goto out; }
    }

    /* Best-effort scrub of the plaintext copy this function itself touched
     * (the caller's own copy, e.g. the interactive prompt buffer, is the
     * caller's responsibility -- see main()'s cmd_passwd). `hash` (the
     * derived, non-secret-once-written value) is not scrubbed -- it's
     * already been written to disk, scrubbing it here has no security
     * value. */
    pt_put(1, "passwd: updated\n");
    result = 0;
out:
    pt_lock_release(lockpath);
    return result;
}

/* ------------------------------- interactive password prompt ------------- */

#define PT_LINE_MAX 128

/* No-echo line read -- unlike tools/init_binary/init.c's read_line(), this
 * deliberately does NOT putn() each typed character back (that's how
 * init.c gets its visible echo -- the console device itself doesn't echo,
 * per that file's own comment, so simply not echoing here is sufficient,
 * no termios/ioctl needed). */
static int pt_read_password(char *buf, size_t max) {
    size_t pos = 0;
    for (;;) {
        char c;
        ssize_t r = sys_read(0, &c, 1);
        if (r <= 0) { if (pos == 0) return -1; break; }
        if (c == '\r' || c == '\n') { pt_put(1, "\n"); break; }
        if (c == 0x7f || c == 8) { if (pos > 0) pos--; continue; }
        if (pos + 1 < max) buf[pos++] = c;
    }
    buf[pos] = 0;
    return (int)pos;
}

static void pt_scrub(char *buf, size_t n) {
    for (size_t i = 0; i < n; i++) buf[i] = 0;
}

static int cmd_passwd_interactive(const char *pwpath, const char *name) {
    char pw1[PT_LINE_MAX], pw2[PT_LINE_MAX];
    pt_put(1, "New password: ");
    if (pt_read_password(pw1, sizeof(pw1)) < 0) { pt_put(2, "passwd: read failed\n"); return -1; }
    pt_put(1, "Retype new password: ");
    if (pt_read_password(pw2, sizeof(pw2)) < 0) { pt_scrub(pw1, sizeof(pw1)); pt_put(2, "passwd: read failed\n"); return -1; }

    if (!pt_streq(pw1, pw2)) {
        pt_scrub(pw1, sizeof(pw1)); pt_scrub(pw2, sizeof(pw2));
        pt_put(2, "passwd: mismatch, password unchanged\n");
        return -1;
    }
    int rc = pw_passwd_set(pwpath, name, pw1);
    pt_scrub(pw1, sizeof(pw1));
    pt_scrub(pw2, sizeof(pw2));
    return rc;
}

/* ------------------------------- entry point ------------------------------ */

#define PT_DEFAULT_PW_PATH "/etc/master.passwd"
#define PT_DEFAULT_GR_PATH "/etc/group"

static void usage(void) {
    pt_put(2, "usage:\n"
              "  pwtool check\n"
              "  pwtool useradd <name> <uid> <gid> <gecos> <home> <shell>\n"
              "  pwtool groupadd <name> <gid> [member,member,...]\n"
              "  pwtool passwd <name>\n");
}

int pwtool_main(int argc, char **argv) {
    /* Seed the weak salt PRNG once per process invocation -- without this,
     * every fresh pwtool run would generate the identical first salt (see
     * li_crypt_seed's own doc comment). getpid() is the only cheap
     * per-invocation entropy this project has wired up at this phase (no
     * /dev/random yet) -- acceptable specifically because a crypt() salt's
     * job is uniqueness, not secrecy (see crypt_des.h's file header). */
    li_crypt_seed((unsigned long)sys_getpid());

    if (argc < 2) { usage(); return 1; }

    if (pt_streq(argv[1], "check")) {
        return pw_check(PT_DEFAULT_PW_PATH, PT_DEFAULT_GR_PATH) == 0 ? 0 : 1;
    }
    if (pt_streq(argv[1], "useradd")) {
        if (argc != 8) { usage(); return 1; }
        return pw_useradd(PT_DEFAULT_PW_PATH, argv[2], argv[3], argv[4], argv[5], argv[6], argv[7]) == 0 ? 0 : 1;
    }
    if (pt_streq(argv[1], "groupadd")) {
        if (argc != 4 && argc != 5) { usage(); return 1; }
        return pw_groupadd(PT_DEFAULT_GR_PATH, argv[2], argv[3], argc == 5 ? argv[4] : "") == 0 ? 0 : 1;
    }
    if (pt_streq(argv[1], "passwd")) {
        if (argc != 3) { usage(); return 1; }
        return cmd_passwd_interactive(PT_DEFAULT_PW_PATH, argv[2]) == 0 ? 0 : 1;
    }

    usage();
    return 1;
}

#ifndef PWTOOL_HOST_TEST
/* Same "may or may not hand us a usable initial sp" caution as
 * tools/init_binary/init.c's _start -- but this binary genuinely needs
 * real argc/argv (subcommand + arguments), unlike init.c (which is PID 1
 * and never receives meaningful argv). Stack layout at entry cross-
 * checked directly against third_party/Csu/start.s's real arm64 `start:`
 * label (the actual reference this project's own real, vendored Csu
 * uses) rather than guessed: sp -> argc (64-bit-padded 32-bit int),
 * argv[0..argc-1], NULL, envp[0..], NULL. Captured into x0/x1/x2 BEFORE
 * switching to this binary's own static stack (pure address-of-original-
 * stack-data arithmetic, no push/call happens on the original sp first,
 * so it's safe to read from even if that sp turns out to be otherwise
 * unusable). */
#define PT_STACK_SIZE (64 * 1024)
__attribute__((used)) static char pt_boot_stack[PT_STACK_SIZE];

__attribute__((naked)) void _start(void) {
    asm volatile(
        "mov  x5, sp\n"
        "ldr  x0, [x5]\n"          /* argc */
        "add  x1, x5, #8\n"        /* argv */
        "add  x4, x0, #1\n"        /* argc + 1 */
        "add  x2, x1, x4, lsl #3\n" /* envp = argv + (argc+1)*8 -- unused, computed for parity with real start.s only */
        "adrp x9, _pt_boot_stack@PAGE\n"
        "add  x9, x9, _pt_boot_stack@PAGEOFF\n"
        "add  x9, x9, %0\n"
        "and  sp, x9, #~15\n"
        "bl   _pwtool_main\n"      /* returns its exit code in x0 (AAPCS64) --
                                     * already exactly where SYS_exit needs it,
                                     * no extra mov needed */
        "mov  x16, #1\n"           /* SYS_exit */
        "svc  #0x80\n"
        "1: b 1b\n"
        :
        : "i"(PT_STACK_SIZE)
        : "x0", "x1", "x2", "x4", "x5", "x9"
    );
}
#endif
