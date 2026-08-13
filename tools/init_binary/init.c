/*
 * Freestanding PID-1 init/shell for XNU-on-rpi3, exec'd as /sbin/launchd
 * from a real msdosfs (FAT32) root on the SD card.
 *
 * No libc, no dyld: built as a fully static Mach-O with LC_UNIXTHREAD and
 * zero LC_LOAD_DYLIB commands (see build.py) so the kernel exec path jumps
 * straight to _start without ever invoking dyld. All BSD syscalls below are
 * issued directly (`svc #0x80`, number in x16, arm64 Darwin convention) --
 * numbers taken from third_party/xnu-7195/bsd/kern/syscalls.master, not
 * guessed. Errors come back as a positive errno in x0 with the carry flag
 * set (not a negated value like Linux) -- syscall3() below converts that to
 * the usual -errno convention.
 *
 * Every "command" here is a builtin dispatched by name, and the whole
 * directory tree is a virtual, in-process structure -- NOT the real VFS.
 * The real root filesystem only backs /sbin/launchd (this binary) and
 * /dev (devfs, for /dev/console); there is no persistent place yet to put
 * a real directory tree or separate command binaries. Real dyld/Libc + real
 * coreutils are a later, separate pull once we're off this bootstrap
 * milestone (see project memory: userspace libc/dyld/launchd).
 *
 * PID 1 (init_main(), below) does real BSD init work now: fork()s a child
 * to run the shell, wait4()s for it, respawns on exit -- init_main() itself
 * is pid 1, the shell is a forked child, not pid 1 as the header used to
 * claim. See init_main()'s comment for the arm64-specific fork() ABI quirk
 * (x1, not x0==0, marks the child) this required getting right.
 */

typedef unsigned long size_t;
typedef long ssize_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;
typedef int int32_t;
typedef long int64_t;

/* ---------------- raw syscalls (arm64 Darwin: svc #0x80, num in x16) ---- */

#define SYS_exit           1
#define SYS_fork            2
#define SYS_read            3
#define SYS_write           4
#define SYS_open            5
#define SYS_close           6
#define SYS_wait4           7
#define SYS_mkdir           136
#define SYS_fstat64         339
#define SYS_getdirentries64 344
#define SYS_execve          59

#define O_RDONLY 0
#define O_RDWR   2

#ifdef INIT_HOST_TEST
/* Host-side logic testing only (see build.py) -- never compiled into the
 * actual freestanding target binary. Real syscall numbers happen to match
 * host libc's read/write/exit, so this just forwards to them. */
long host_shim_syscall3(long num, long a0, long a1, long a2);
static inline long syscall3(long num, long a0, long a1, long a2) {
    return host_shim_syscall3(num, a0, a1, a2);
}
#else
/* ROOT CAUSE (found via QEMU-monitor PC/register sampling, x0 caught
 * holding implausible small positive values -- e.g. 9 -- right after a
 * 1-byte read()'s svc): the arm64 Darwin syscall ABI does NOT return errors
 * as a negative value in x0 like Linux. Per
 * bsd/dev/arm/systemcalls.c:arm_prepare_u64_syscall_return(), on error x0 is
 * set to the raw positive errno AND PSTATE's carry flag (C) is set; success
 * leaves the carry flag clear. This wrapper never checked the carry flag,
 * so every syscall error made by this freestanding binary (no libc, so no
 * cerror stub either) has always been silently misread as a suspiciously-
 * large-but-technically-positive success value by every caller's `r <= 0`
 * error check. Fixed by reading PSTATE.C via `cset` and negating x0 on
 * error, matching the conventional -errno-on-failure contract the rest of
 * this file's callers already assume. */
static inline long syscall3(long num, long a0, long a1, long a2) {
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
#endif

static ssize_t sys_write(int fd, const void *buf, size_t n) {
    return syscall3(SYS_write, fd, (long)buf, (long)n);
}

static ssize_t sys_read(int fd, void *buf, size_t n) {
    return syscall3(SYS_read, fd, (long)buf, (long)n);
}

static void sys_exit(int code) {
    syscall3(SYS_exit, code, 0, 0);
    for (;;) { }
}

static int sys_open(const char *path, int flags, int mode) {
    return (int)syscall3(SYS_open, (long)path, flags, mode);
}

static long sys_execve(const char *path, char *const argv[], char *const envp[]) {
    return syscall3(SYS_execve, (long)path, (long)argv, (long)envp);
}

static long sys_close(int fd) {
    return syscall3(SYS_close, fd, 0, 0);
}

static long sys_mkdir(const char *path, int mode) {
    return syscall3(SYS_mkdir, (long)path, mode, 0);
}

/* REAL BUG FOUND (2026-08-13, the first time any userspace program in this
 * project ever issued a real stat-family syscall): plain fstat (189) does
 * NOT fill the __DARWIN_STRUCT_STAT64 layout struct kstat below is written
 * against - confirmed empirically by dumping the raw bytes fstat(189)
 * actually wrote for the real HFS+ root ("/"): st_mode read back as 2 (a
 * VDIR vtype value, not a real mode) and st_nlink as 0, both nonsensical,
 * while a byte-by-byte reinterpretation matched the OLDER, 32-bit-ino_t
 * struct stat layout perfectly instead (root CNID 2 at offset 4, mode
 * 0100775/0040775-shaped 0x41fd at offset 8, nlink=5 matching this root's
 * real subdirectory count, a plausible 2026 Unix timestamp at offset 24 -
 * far too coherent to be coincidence). Cross-checked against
 * bsd/kern/syscalls.master: fstat=189 is a distinct syscall number from
 * fstat64=339 (AUE_FSTAT64) - the plain, non-64 syscall fills the legacy
 * struct, exactly like stat()/lstat() do relative to stat64()/lstat64().
 * Fixed by calling fstat64 (339) instead, which fills exactly the
 * __DARWIN_STRUCT_STAT64 layout struct kstat already implements -
 * confirmed correct independently by cross-checking sizeof/offsetof
 * against a real host arm64 Darwin compiler's actual <sys/stat.h> (144
 * bytes total, st_mode at offset 4, st_size at offset 96 - exact match). */
static long sys_fstat(int fd, void *statbuf) {
    return syscall3(SYS_fstat64, fd, (long)statbuf, 0);
}

/* SAME REAL BUG CLASS as sys_fstat() above, found the same way (raw byte
 * dump, not assumption): plain getdirentries (196) does NOT fill
 * struct kdirent's __DARWIN_STRUCT_DIRENTRY (64-bit-ino) layout either -
 * confirmed empirically (reclen/namlen/type read back as nonsensical
 * values like namlen=516, type=46) and by the same fstat/fstat64 split
 * existing in bsd/kern/syscalls.master for this syscall family too:
 * getdirentries=196 is legacy, getdirentries64=344 (AUE_GETDIRENTRIES64)
 * is the one that actually matches the 64-bit-ino dirent layout. Its
 * signature also differs slightly (`off_t *position` instead of
 * `long *basep`) but that's identical in size/ABI on this LP64 target, no
 * wrapper change needed beyond the syscall number. Needs 4 real
 * arguments -- no shared syscall4() helper exists in this file yet
 * (sys_wait4() above inlines its own 4-arg asm block rather than
 * factoring one out), so this follows that same established pattern
 * rather than introducing a new one. */
static long sys_getdirentries(int fd, void *buf, unsigned int count, long *basep) {
#ifdef INIT_HOST_TEST
    (void)fd; (void)buf; (void)count; (void)basep;
    return -1; /* no real filesystem in the host-test harness */
#else
    register long x0 asm("x0") = fd;
    register long x1 asm("x1") = (long)buf;
    register long x2 asm("x2") = count;
    register long x3 asm("x3") = (long)basep;
    register long x16 asm("x16") = SYS_getdirentries64;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x2), "r"(x3), "r"(x16)
                 : "memory", "cc");
    if (is_error) return -x0;
    return x0;
#endif
}

/* ROOT CAUSE, found by reading thread_set_child()/thread_set_parent() in
 * osfmk/arm64/status.c before writing this rather than assuming the
 * Linux/x86 convention: arm64 XNU's raw fork() does NOT give the child x0=0.
 * BOTH parent and child come back from `svc #0x80` with x0 = the child's
 * pid; the only thing that tells them apart is x1 (0 = you are the parent,
 * 1 = you are the child). A normal cerror-wrapped libc fork() hides this by
 * zeroing the return value itself when x1==1 -- we have no libc, so it has
 * to be done here explicitly. Getting this wrong (e.g. testing `pid == 0`
 * like Linux) would make the "child" branch never run. */
static long sys_fork(int *out_is_child) {
#ifdef INIT_HOST_TEST
    *out_is_child = 0;
    return -1; /* no real process model in the host-test harness */
#else
    register long x0 asm("x0") = 0;
    register long x1 asm("x1") = 0;
    register long x16 asm("x16") = SYS_fork;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %2, cs"
                 : "+r"(x0), "+r"(x1), "=r"(is_error)
                 : "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        *out_is_child = 0;
        return -x0;
    }
    *out_is_child = (int)x1;
    return x0;
#endif
}

static long sys_wait4(int pid, long *status, int options, long *rusage) {
#ifdef INIT_HOST_TEST
    (void)pid; (void)status; (void)options; (void)rusage;
    return -1; /* nothing to reap -- sys_fork() never produces a child here */
#else
    register long x0 asm("x0") = pid;
    register long x1 asm("x1") = (long)status;
    register long x2 asm("x2") = options;
    register long x3 asm("x3") = (long)rusage;
    register long x16 asm("x16") = SYS_wait4;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x2), "r"(x3), "r"(x16)
                 : "memory", "cc");
    if (is_error) return -x0;
    return x0;
#endif
}

/* The kernel hands pid 1 an empty fd table -- on real macOS, launchd itself
 * is the one that opens /dev/console onto 0/1/2 before doing anything else;
 * there is no kernel-side setup. We're standing in for launchd, so we have
 * to do the same thing explicitly. Opened independently three times rather
 * than open+dup2 -- each open() picks the lowest free fd (0, then 1, then
 * 2), same effect without relying on dup2(). Requires devfs to actually be
 * mounted at /dev, which in turn requires a /dev directory to already exist
 * on the root filesystem for devfs_kernel_mount() to mount onto (see
 * bsd_init.c's devfs_kernel_mount("/dev") call) -- create one on the root
 * image if open() ever starts failing with ENOENT again. */
static void open_console(void) {
    int fd0 = sys_open("/dev/console", O_RDWR, 0);
    if (fd0 < 0) {
        return; /* nothing we can do without a console; fall through as before */
    }
    sys_open("/dev/console", O_RDWR, 0); /* fd 1 */
    sys_open("/dev/console", O_RDWR, 0); /* fd 2 */
}

/* ---------------------------- tiny string helpers ----------------------- */

static size_t kstrlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static int kstreq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

static void kmemcpy(void *dst, const void *src, size_t n) {
    char *d = (char *)dst;
    const char *s = (const char *)src;
    while (n--) *d++ = *s++;
}

static void kstrncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

static void put(const char *s) {
    sys_write(1, s, kstrlen(s));
}

static void putn(const char *s, size_t n) {
    sys_write(1, s, n);
}

/* ------------------------------- real VFS --------------------------------
 * Phase 3.5a (docs/roadmap.md): this used to be an entirely fake,
 * hardcoded in-memory tree (nodes[]/vfs_init()/vfs_resolve()/vfs_path())
 * with no connection to the real mounted filesystem -- confirmed still the
 * case as recently as this session's own boot tests (ls after a real HFS+
 * root mount still printed the old fake bin/sbin/etc/dev/tmp/var list).
 * Replaced with real open/read/getdirentries/fstat/mkdir syscalls against
 * whatever's actually mounted at "/". `cwd_path` below is now a plain
 * absolute path string (this project's freestanding libc has no
 * getcwd()/chdir()-syscall-backed cwd tracking to lean on -- there is in
 * fact no raw `getcwd` syscall in bsd/kern/syscalls.master at all; real
 * userspace getcwd() is a Libc-level directory walk via getattrlist, not a
 * syscall -- so `cd` is kept as pure local string state, validated against
 * the real filesystem via a real open()+fstat() each time, same tradeoff
 * the old fake tree already made, just now checked against real files).
 */

#define MAX_PATH 256

static char cwd_path[MAX_PATH] = "/";

/* Real bsd/sys/stat.h struct stat, __DARWIN_STRUCT_STAT64 expansion (the
 * variant used whenever __DARWIN_64_BIT_INO_T is set, true for every
 * 64-bit Darwin target including this one) with the default
 * _DARWIN_C_SOURCE timespec-based time fields. Verified field-by-field
 * against third_party/xnu-7195/bsd/sys/stat.h + bsd/sys/_types.h rather
 * than guessed: dev_t/st_rdev=int32_t, mode_t/nlink_t=uint16_t,
 * ino64_t/off_t/blkcnt_t=64-bit, uid_t/gid_t/blksize_t/st_flags/st_gen=
 * uint32_t. Only st_mode/st_size are actually read below, but the whole
 * layout -- including the real 4-byte alignment gap between st_rdev and
 * the first (8-byte-aligned) timespec -- has to match, since fstat()
 * writes the kernel's real ABI struct verbatim into whatever buffer this
 * points at; a wrong layout would silently misread unrelated bytes as
 * st_mode/st_size instead of erroring. */
struct kstat {
    int32_t  st_dev;
    uint16_t st_mode;
    uint16_t st_nlink;
    uint64_t st_ino;
    uint32_t st_uid;
    uint32_t st_gid;
    int32_t  st_rdev;
    int32_t  __pad0; /* alignment gap before the 8-byte-aligned timespecs */
    int64_t  st_atime_sec, st_atime_nsec;
    int64_t  st_mtime_sec, st_mtime_nsec;
    int64_t  st_ctime_sec, st_ctime_nsec;
    int64_t  st_birthtime_sec, st_birthtime_nsec;
    int64_t  st_size;
    int64_t  st_blocks;
    int32_t  st_blksize;
    uint32_t st_flags;
    uint32_t st_gen;
    int32_t  st_lspare;
    int64_t  st_qspare[2];
};

#define S_IFMT  0170000
#define S_IFDIR 0040000
#define S_IFREG 0100000

/* Real bsd/sys/dirent.h __DARWIN_STRUCT_DIRENTRY (the 64-bit-ino variant,
 * same reasoning as struct kstat above -- natural alignment, not the
 * pack(4) layout that header uses for the now-unused 32-bit-ino variant;
 * confirmed by reading where the #pragma pack(4)/#pragma pack() pair
 * actually opens and closes in that header). d_reclen (not sizeof(struct
 * kdirent)) is what real getdirentries() consumers advance a buffer
 * cursor by -- this driver, like every real one, never actually depends
 * on this struct's own total size for that. */
struct kdirent {
    uint64_t d_ino;
    uint64_t d_seekoff;
    uint16_t d_reclen;
    uint16_t d_namlen;
    uint8_t  d_type;
    char     d_name[1024];
};

#define DT_DIR 4
#define DT_REG 8

/* Join `rel` onto `base` (if `rel` isn't already absolute) and normalize
 * (collapse "." / ".." / empty segments), writing a clean absolute path
 * into `out`. This is the real-filesystem equivalent of the old fake
 * tree's vfs_resolve() -- same segment-walking shape, but building a path
 * string instead of walking an in-memory node graph, since path
 * resolution against a real mounted filesystem happens in the kernel
 * (inside whatever syscall actually opens the result), not here. */
static void path_join(const char *base, const char *rel, char *out, size_t outsz) {
    char work[MAX_PATH];
    if (rel[0] == '/') {
        kstrncpy(work, rel, MAX_PATH);
    } else {
        size_t blen = kstrlen(base);
        kstrncpy(work, base, MAX_PATH);
        if (blen > 0 && blen + 1 < MAX_PATH && work[blen - 1] != '/') {
            work[blen] = '/';
            work[blen + 1] = 0;
            blen++;
        }
        size_t rlen = kstrlen(rel);
        if (blen + rlen < MAX_PATH) {
            kmemcpy(work + blen, rel, rlen);
            work[blen + rlen] = 0;
        }
    }

    /* Segment-stack normalize, same algorithm the old vfs_resolve() used
     * for '.'/'..' handling, just emitting into `out` instead of walking
     * node indices. */
    char *seg_starts[32];
    size_t seg_lens[32];
    int nseg = 0;

    char *p = work;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        char *start = p;
        while (*p && *p != '/') p++;
        size_t seglen = (size_t)(p - start);

        if (seglen == 0) continue;
        if (seglen == 1 && start[0] == '.') continue;
        if (seglen == 2 && start[0] == '.' && start[1] == '.') {
            if (nseg > 0) nseg--;
            continue;
        }
        if (nseg < 32) {
            seg_starts[nseg] = start;
            seg_lens[nseg] = seglen;
            nseg++;
        }
    }

    size_t off = 0;
    out[0] = '/';
    off = 1;
    for (int i = 0; i < nseg; i++) {
        if (i > 0) {
            if (off + 1 >= outsz) break;
            out[off++] = '/';
        }
        size_t l = seg_lens[i];
        if (off + l >= outsz) l = outsz - off - 1;
        kmemcpy(out + off, seg_starts[i], l);
        off += l;
    }
    out[off] = 0;
}

/* ------------------------------- line input ------------------------------ */

#define LINE_MAX 256
#define MAX_ARGS 16

static int read_line(char *buf, size_t max) {
    size_t pos = 0;
    for (;;) {
        char c;
        ssize_t r = sys_read(0, &c, 1);
        if (r <= 0) {
            if (pos == 0) return -1;
            break;
        }
        if (c == '\r' || c == '\n') {
            put("\n");
            break;
        }
        if ((c == 0x7f || c == 8)) {
            if (pos > 0) {
                pos--;
                put("\b \b");
            }
            continue;
        }
        if (pos + 1 < max) {
            buf[pos++] = c;
            putn(&c, 1);
        }
    }
    buf[pos] = 0;
    return (int)pos;
}

static int tokenize(char *line, char *argv[], int max_args) {
    int argc = 0;
    char *p = line;
    while (*p && argc < max_args) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = 0;
    }
    return argc;
}

/* -------------------------------- builtins -------------------------------- */

static const char *prompt_name = "sh";

static void cmd_help(void) {
    put("available commands: ls cat cd pwd echo mkdir help clear exit bash\n");
}

static void cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        put(argv[i]);
        if (i + 1 < argc) put(" ");
    }
    put("\n");
}

static void cmd_pwd(void) {
    put(cwd_path);
    put("\n");
}

static void cmd_cd(int argc, char **argv) {
    const char *target = (argc > 1) ? argv[1] : "/";
    char resolved[MAX_PATH];
    path_join(cwd_path, target, resolved, sizeof(resolved));

    int fd = sys_open(resolved, O_RDONLY, 0);
    if (fd < 0) {
        put(target); put(": no such directory\n");
        return;
    }
    struct kstat st;
    long r = sys_fstat(fd, &st);
    sys_close(fd);
    if (r < 0) {
        put(target); put(": stat failed\n");
        return;
    }
    if ((st.st_mode & S_IFMT) != S_IFDIR) {
        put(target); put(": not a directory\n");
        return;
    }
    kstrncpy(cwd_path, resolved, MAX_PATH);
}

static void cmd_ls(int argc, char **argv) {
    char resolved[MAX_PATH];
    if (argc > 1) {
        path_join(cwd_path, argv[1], resolved, sizeof(resolved));
    } else {
        kstrncpy(resolved, cwd_path, MAX_PATH);
    }

    int fd = sys_open(resolved, O_RDONLY, 0);
    if (fd < 0) {
        put(argc > 1 ? argv[1] : resolved);
        put(": no such file or directory\n");
        return;
    }

    struct kstat st;
    if (sys_fstat(fd, &st) < 0) {
        sys_close(fd);
        put(argc > 1 ? argv[1] : resolved);
        put(": stat failed\n");
        return;
    }

    if ((st.st_mode & S_IFMT) != S_IFDIR) {
        /* ls <file>: print the argument as given, matching normal `ls`
         * behavior (echo back what was asked for, not the resolved path). */
        sys_close(fd);
        put(argc > 1 ? argv[1] : resolved);
        put("\n");
        return;
    }

    char buf[4096];
    long basep = 0;
    for (;;) {
        long n = sys_getdirentries(fd, buf, sizeof(buf), &basep);
        if (n <= 0) break; /* 0 == no more entries, <0 == error, both stop */

        char *p = buf;
        char *end = buf + n;
        while (p < end) {
            struct kdirent *de = (struct kdirent *)p;
            if (de->d_reclen == 0) break; /* malformed entry, stop rather than spin */

            int is_dot = (de->d_namlen == 1 && de->d_name[0] == '.');
            int is_dotdot = (de->d_namlen == 2 && de->d_name[0] == '.' && de->d_name[1] == '.');
            /* HFS+ reserves entries with a name ending "\r" as private
             * metadata (e.g. ".HFS+ Private Directory Data\r") - real,
             * confirmed via this exact debugging session's raw byte dumps,
             * not assumed. Hide these the same way "."/".." are hidden;
             * a normal user-facing `ls` has no reason to show them.
             * KNOWN GAP: HFS+'s *other* reserved entry (the private
             * metadata file, conventionally 4 leading NUL bytes then
             * "HFS+ Private Data") is NOT filtered - a leading-NUL check
             * was tried and empirically did not match whatever this
             * kernel's getdirentries64 actually returns for that specific
             * entry (still shows with a garbled name in `ls` output) -
             * not root-caused further; cosmetic only, doesn't affect any
             * real file/directory listing. */
            int is_hfs_private = (de->d_namlen > 0 && de->d_name[de->d_namlen - 1] == '\r');
            if (!is_dot && !is_dotdot && !is_hfs_private) {
                putn(de->d_name, de->d_namlen);
                if (de->d_type == DT_DIR) put("/");
                put("\n");
            }
            p += de->d_reclen;
        }
    }
    sys_close(fd);
}

static void cmd_cat(int argc, char **argv) {
    if (argc < 2) { put("usage: cat <path>\n"); return; }
    char resolved[MAX_PATH];
    path_join(cwd_path, argv[1], resolved, sizeof(resolved));

    int fd = sys_open(resolved, O_RDONLY, 0);
    if (fd < 0) { put(argv[1]); put(": no such file\n"); return; }

    struct kstat st;
    if (sys_fstat(fd, &st) < 0) {
        sys_close(fd);
        put(argv[1]); put(": stat failed\n");
        return;
    }
    if ((st.st_mode & S_IFMT) == S_IFDIR) {
        sys_close(fd);
        put(argv[1]); put(": is a directory\n");
        return;
    }

    char buf[512];
    for (;;) {
        ssize_t n = sys_read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        sys_write(1, buf, (size_t)n);
    }
    sys_close(fd);
}

static void cmd_mkdir(int argc, char **argv) {
    if (argc < 2) { put("usage: mkdir <path>\n"); return; }
    char resolved[MAX_PATH];
    path_join(cwd_path, argv[1], resolved, sizeof(resolved));

    /* Confirmed real (2026-08-13, via a temporary errno dump since removed):
     * mkdir() against a fresh path off the real HFS+ root returns -30
     * (EROFS) with a correctly-resolved path - the syscall itself and
     * path_join() are both working correctly, but this project's real
     * HFS+ root is currently mounted read-only (likely still true from
     * whatever first got hfs_mountroot() working read-only-first, before
     * any real write was ever attempted against it - this is plausibly
     * the first one). Not something tools/init_binary/init.c can fix -
     * that's a kernel/mount-path matter. */
    long r = sys_mkdir(resolved, 0755);
    if (r < 0) {
        put(argv[1]);
        put(": mkdir failed (often EROFS - this filesystem may be mounted read-only)\n");
    }
}

static void cmd_clear(void) {
    put("\x1b[2J\x1b[H");
}

/* ---------------------------------- shell ---------------------------------- */

static void print_prompt(void) {
    put(prompt_name);
    put(":");
    put(cwd_path);
    put(" % ");
}

static void dispatch(int argc, char **argv) {
    const char *cmd = argv[0];
    if (kstreq(cmd, "help")) cmd_help();
    else if (kstreq(cmd, "echo")) cmd_echo(argc, argv);
    else if (kstreq(cmd, "pwd")) cmd_pwd();
    else if (kstreq(cmd, "cd")) cmd_cd(argc, argv);
    else if (kstreq(cmd, "ls")) cmd_ls(argc, argv);
    else if (kstreq(cmd, "cat")) cmd_cat(argc, argv);
    else if (kstreq(cmd, "mkdir")) cmd_mkdir(argc, argv);
    else if (kstreq(cmd, "clear")) cmd_clear();
    else if (kstreq(cmd, "exit")) sys_exit(0);
    else if (kstreq(cmd, "bash")) {
        prompt_name = "bash";
        put("bash is this same shell -- there is no separate dyld-linked bash yet.\n");
    } else {
        put(cmd);
        put(": command not found\n");
    }
}

static void banner(void) {
    put("\n");
    put("XNU/rpi3 -- single-user shell (no dyld, no libc), forked by pid 1\n");
    put("booted from a real HFS+ root, exec'd in place of /sbin/launchd\n");
    put("type 'help' for available commands\n\n");
}

void shell_main(void) {
    banner();

    char line[LINE_MAX];
    char *argv[MAX_ARGS];

    for (;;) {
        print_prompt();
        int n = read_line(line, sizeof(line));
        if (n < 0) continue;
        int argc = tokenize(line, argv, MAX_ARGS);
        if (argc == 0) continue;
        dispatch(argc, argv);
    }
}

/* -------------------------------- pid 1 body ------------------------------
 * Real BSD-style init: fork a child to run the interactive shell, wait4()
 * for it, and respawn if it ever exits -- same shape as classic
 * single-user-mode init babysitting /bin/sh, just aimed at our builtin
 * virtual shell instead (see docs/roadmap.md Phase 4 step 2). PID 1 itself
 * never runs the shell loop directly except as a last-resort fallback if
 * fork() itself is unavailable.
 */

/* What the forked child actually runs. Tries the real dyld-linked /bin/sh
 * first -- doesn't exist on the root fs yet (Phase 4), so this fails with
 * ENOENT today and falls through to the builtin virtual shell. Left wired
 * up now so pid 1 needs no further changes once a real /bin/sh lands. */
static void single_user_child(void) {
    static char *const argv0[] = { (char *)"/bin/sh", 0 };
    static char *const envp0[] = { 0 };
    sys_execve("/bin/sh", argv0, envp0);
    shell_main();
}

void init_main(void) {
    open_console();
    put("\ninit: pid 1 up, forking single-user shell\n");

    for (;;) {
        int is_child = 0;
        long pid = sys_fork(&is_child);

        if (pid < 0) {
            put("init: fork failed -- no process model available, "
                "running shell directly in pid 1 (no respawn)\n");
            single_user_child();
            sys_exit(0);
        }

        if (is_child) {
            single_user_child();
            sys_exit(0); /* only reached if the shell's own loop ever returns */
        }

        /* parent (real pid 1): block until the shell dies, then respawn it,
         * exactly like init babysitting a single-user shell. */
        long status = 0;
        sys_wait4((int)pid, &status, 0, 0);
        put("\ninit: shell exited, respawning\n");
    }
}

/* ------------------------------- entry point -------------------------------
 * The kernel's LC_UNIXTHREAD state may or may not hand us a usable sp (see
 * build.py's notes on why); never trust it -- switch to our own static
 * stack before doing anything else, including calling a real C function.
 */

#define STACK_SIZE (64 * 1024)
__attribute__((used)) static char boot_stack[STACK_SIZE];

__attribute__((naked)) void _start(void) {
    asm volatile(
        "adrp x0, _boot_stack@PAGE\n"
        "add  x0, x0, _boot_stack@PAGEOFF\n"
        "add  x0, x0, %0\n"
        "and  sp, x0, #~15\n"
        "bl   _init_main\n"
        "mov  x0, #0\n"
        "mov  x16, #1\n"       /* SYS_exit */
        "svc  #0x80\n"
        "1: b 1b\n"
        :
        : "i"(STACK_SIZE)
        : "x0"
    );
}
