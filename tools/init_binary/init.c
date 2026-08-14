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

/* Guarded with the SAME macro names Apple's real SDK headers use for each
 * of these (confirmed by reading e.g.
 * .../usr/include/_types/_uint64_t.h's own `#ifndef _UINT64_T` -- not
 * guessed) -- purely a HOST-TEST-BUILD concern: the real freestanding
 * target (-ffreestanding -nostdlib, no system headers at all) never
 * defines these macros, so the guards are no-ops there and every typedef
 * below still applies exactly as before. Needed because
 * tools/init_binary/host_test.c's shim (added for Phase 5's login host
 * tests) has to pull in real <fcntl.h>/<unistd.h> for its own open/read/
 * write/close forwarding, and those transitively define uint64_t as
 * `unsigned long long` (vs. this file's own pre-existing `unsigned
 * long`) -- both are real 8-byte unsigned types on this LP64 target, this
 * is a naming/redeclaration technicality only, not a behavioral
 * difference; a real, reproduced build error (not a hypothetical) is what
 * revealed the need for these guards, see host_test.c's own header
 * comment. size_t/ssize_t/int32_t are NOT guarded -- their host-SDK
 * definitions (`unsigned long`/`long`/`int`) already match this file's
 * own exactly on this LP64 target, so a plain re-typedef to the identical
 * underlying type is legal C with no conflict, confirmed by the same
 * build error output only ever naming uint64_t/int64_t. */
typedef unsigned long size_t;
typedef long ssize_t;
#ifndef _UINT8_T
#define _UINT8_T
typedef unsigned char uint8_t;
#endif
#ifndef _UINT16_T
#define _UINT16_T
typedef unsigned short uint16_t;
#endif
#ifndef _UINT32_T
#define _UINT32_T
typedef unsigned int uint32_t;
#endif
#ifndef _UINT64_T
#define _UINT64_T
typedef unsigned long uint64_t;
#endif
typedef int int32_t;
#ifndef _INT64_T
#define _INT64_T
typedef long int64_t;
#endif

/* ---------------- raw syscalls (arm64 Darwin: svc #0x80, num in x16) ---- */

#define SYS_exit           1
#define SYS_fork            2
#define SYS_read            3
#define SYS_write           4
#define SYS_open            5
#define SYS_close           6
#define SYS_wait4           7
#define SYS_setuid          23
#define SYS_getuid          24
#define SYS_getgid          47
#define SYS_setgroups       80
#define SYS_mkdir           136
#define SYS_setgid          181
#define SYS_fstat64         339
#define SYS_getdirentries64 344
#define SYS_execve          59

/* Phase 5: real login/su, numbers cross-checked against
 * third_party/xnu-7195/bsd/kern/syscalls.master the same way every other
 * syscall number in this file already is (23=setuid, 24=getuid,
 * 47=getgid, 80=setgroups, 181=setgid) -- not guessed. */

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

#define SYS_chmod 15
#define SYS_chown 16

static long sys_chown(const char *path, unsigned int uid, unsigned int gid) {
    return syscall3(SYS_chown, (long)path, uid, gid);
}
static long sys_chmod(const char *path, int mode) {
    return syscall3(SYS_chmod, (long)path, mode, 0);
}

static void put(const char *s);
static void putint(long v);

#define SYS_mount 167

/* Real BSD root-mount semantics, confirmed by reading
 * bsd/vfs/vfs_subr.c:vfs_mountroot() (`mp->mnt_flag = MNT_RDONLY |
 * MNT_ROOTFS`, hardcoded) and bsd/vfs/vfs_syscalls.c:__mac_mount(): root is
 * ALWAYS mounted read-only first, same as real BSD/macOS -- userland (real
 * launchd, or this file standing in for it) is expected to remount it
 * read-write itself (the real-world equivalent of `mount -uw /`). Without
 * this, every write anywhere on "/" (pwtool provisioning /etc/master.passwd,
 * mkdir for home directories, etc) fails EROFS -- not a bug in those
 * callers, a missing step here.
 *
 * __mac_mount() special-cases the root vnode: mounting path "/" forces
 * MNT_UPDATE onto `flags` automatically even if we pass 0, so flags=0 (i.e.
 * "not read-only") is sufficient -- see that function's `if ((vp->v_flag &
 * VROOT) && (vp->v_mount->mnt_flag & MNT_ROOTFS))` block. What IS required
 * (confirmed in hfs_mount(), bsd/hfs/hfs_vfsops.c): `hfs_assert(data)` fires
 * whenever MNT_UPDATE is set, so `data` must be a real, non-NULL pointer to
 * a struct hfs_mount_args, not NULL -- a zeroed one is fine for a plain
 * write-upgrade (those fields are only meaningful for a fresh/wrapper HFS
 * mount, not this update path). This mirrors real mount_hfs(8)'s own
 * behavior of always passing a fully-populated args struct, just zeroed
 * here since we're not doing an initial mount.
 *
 * Struct layout below matches the KERNEL-side struct hfs_mount_args
 * (bsd/hfs/hfs_mount.h) exactly -- that header #ifndef KERNELs out the
 * `fspec` field, and hfs_mount() copyin()s directly into that KERNEL-side
 * struct, so this userspace copy must match it field-for-field (uid_t/gid_t
 * = 4 bytes each, mode_t = 2 bytes + 2 bytes padding before the next
 * u_int32_t, struct timezone = two ints = 8 bytes), not the userspace-visible
 * one applications normally see. Sizes cross-checked against this project's
 * own _types headers (_uid_t.h/_gid_t.h -> __uint32_t, _mode_t.h ->
 * __uint16_t), not assumed. */
struct hfs_mount_args_stub {
    unsigned int hfs_uid;
    unsigned int hfs_gid;
    unsigned short hfs_mask;
    unsigned short _pad;
    unsigned int hfs_encoding;
    int tz_minuteswest;
    int tz_dsttime;
    int flags;
    int journal_tbuffer_size;
    int journal_flags;
    int journal_disable;
};

static long sys_mount(const char *type, const char *path, int flags, void *data) {
    register long x0 asm("x0") = (long)type;
    register long x1 asm("x1") = (long)path;
    register long x2 asm("x2") = flags;
    register long x3 asm("x3") = (long)data;
    register long x16 asm("x16") = SYS_mount;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x2), "r"(x3), "r"(x16)
                 : "memory", "cc");
    if (is_error) return -x0;
    return x0;
}

/* Remount "/" read-write. Called once by PID 1 at boot, before the first
 * login/shell -- real launchd-equivalent bootstrap step, not per-session.
 * Best-effort: logs and continues either way, since a read-only root isn't
 * fatal to booting a shell, only to anything that writes (pwtool, mkdir,
 * etc) -- those already fail loudly and specifically (EROFS) on their own,
 * so this doesn't need to gate anything downstream. */
static void remount_root_rw(void) {
#ifdef INIT_HOST_TEST
    return;
#else
    struct hfs_mount_args_stub args;
    for (size_t i = 0; i < sizeof(args); i++) ((char *)&args)[i] = 0;
    long rc = sys_mount("hfs", "/", 0, &args);
    if (rc < 0) {
        put("init: remount / read-write failed, rc="); putint(rc); put(" -- staying read-only\n");
    } else {
        put("init: / remounted read-write\n");
    }
#endif
}

/* Phase 5: real credential syscalls for do_login()/cmd_su() below. All fit
 * the existing generic 3-arg syscall3() helper (no extra registers needed,
 * unlike fork/wait4/getdirentries), same as sys_open/sys_close/sys_mkdir
 * above -- no separate INIT_HOST_TEST stub needed per-function; in host-test
 * mode these simply forward to host_shim_syscall3() like everything else
 * using the shared syscall3() wrapper (host shim doesn't recognize these
 * numbers, returns -1 -- acceptable, host tests exercise the login DECISION
 * logic (attempt_login()) directly, not the real privilege-drop syscalls,
 * which need a real kernel to mean anything anyway). */
static long sys_setuid(unsigned int uid) {
    return syscall3(SYS_setuid, uid, 0, 0);
}
static long sys_setgid(unsigned int gid) {
    return syscall3(SYS_setgid, gid, 0, 0);
}
static long sys_setgroups(unsigned int gidsetsize, unsigned int *gidset) {
    return syscall3(SYS_setgroups, gidsetsize, (long)gidset, 0);
}
static long sys_getuid(void) {
    return syscall3(SYS_getuid, 0, 0, 0);
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

/* ------------------------------- Phase 5: real login/su ------------------
 * Wires tools/libinfo_custom's flat-file resolver + DES-crypt port into a
 * real authenticated session, replacing what used to be an entirely
 * implicit single user: previously, this binary simply ran as whatever
 * uid the kernel exec'd it as (root -- pid 1 always is) for its entire
 * life, with no login prompt, no setuid() call anywhere in this file, and
 * no notion of "who is running this shell" at all -- confirmed by grepping
 * this file before this change: zero references to uid/gid/passwd
 * anywhere. That's the exact integration point this Phase 5 pass replaces.
 *
 * Deliberately freestanding/no-libc, same discipline as the rest of this
 * file -- libinfo_custom.c and crypt_des.c are pulled in as sibling
 * translation units (see build.py), not a library dependency. */
#include "../libinfo_custom/libinfo_custom.h"
#include "../libinfo_custom/crypt_des.h"

#define MASTER_PASSWD_PATH "/etc/master.passwd"

/* Current session identity. uid/gid start at whatever the kernel handed
 * this process (root, pid 1's child) -- do_login() either drops them for
 * real via a real setuid()/setgid() (see drop_privileges() below) or, in
 * the deliberate no-account-database-yet bootstrap case, leaves them
 * alone and current_user stays "root", matching this project's prior,
 * already-verified boot behavior exactly (see this section's own
 * do_login() comment for why that fallback exists and what it does NOT
 * weaken). */
static char current_user[64] = "root";
static unsigned int current_uid = 0;
static unsigned int current_gid = 0;

#define PASSWORD_MAX 128

/* Same shape as read_line() above, but does NOT putn() typed characters
 * back -- the only difference a password prompt needs. This project's
 * /dev/console itself does not echo (read_line()'s own visible-echo
 * behavior is done entirely in software, per that function's design --
 * confirmed by reading it: every accepted byte is explicitly putn()'d),
 * so simply skipping that one call is sufficient for a real no-echo
 * password prompt; no termios/ioctl work needed. Backspace is still
 * honored (so a mistyped password can be corrected) but produces no
 * visual feedback either, for the same reason. */
static int read_line_noecho(char *buf, size_t max) {
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
        if (c == 0x7f || c == 8) {
            if (pos > 0) pos--;
            continue;
        }
        if (pos + 1 < max) {
            buf[pos++] = c;
        }
    }
    buf[pos] = 0;
    return (int)pos;
}

static void scrub(char *buf, size_t n) {
    for (size_t i = 0; i < n; i++) buf[i] = 0;
}

/* setgroups -> setgid -> setuid, in that exact order (must happen while
 * still root -- once setuid() to a non-zero uid succeeds, this process
 * permanently loses the privilege to call the other two, real BSD/XNU
 * kernel-enforced semantics, not something this file has to re-check
 * itself). FAIL-CLOSED: any failure partway through is treated as fatal
 * by every caller below (do_login()/cmd_su() both sys_exit()/abort rather
 * than continue running with a partially-dropped or ambiguous privilege
 * state -- a partial drop succeeding on setgid but failing on setuid,
 * left unchecked, would run an authenticated "user" shell with the
 * REAL uid still 0/root, a real privilege-escalation-shaped bug). */
static int drop_privileges(unsigned int uid, unsigned int gid) {
    unsigned int gidset[1];
    gidset[0] = gid;
    if (sys_setgroups(1, gidset) < 0) return -1;
    if (sys_setgid(gid) < 0) return -1;
    if (sys_setuid(uid) < 0) return -1;
    return 0;
}

#define LOGIN_OK              0
#define LOGIN_BAD_CREDENTIALS 1
#define LOGIN_NO_ACCOUNTS     2
#define LOGIN_AUTH_BROKEN     3

/* Core login decision logic, deliberately separated from any actual
 * prompting/I/O (do_login() below does that) so it's directly
 * host-testable (see tools/init_binary/host_test.c) the same way
 * tools/pwtool/pwtool.c splits pw_passwd_set() from its interactive
 * wrapper. `pwpath` is a parameter rather than hardcoded specifically so
 * host tests can point it at a temp fixture instead of the real
 * /etc/master.passwd.
 *
 * Returns one of the four codes above:
 *   LOGIN_OK              -- *out_pw is the real, authenticated account.
 *   LOGIN_BAD_CREDENTIALS -- wrong username and/or password. Deliberately
 *                            a single, undifferentiated outcome (does NOT
 *                            distinguish "no such user" from "wrong
 *                            password" to the caller) -- classic
 *                            anti-enumeration practice, reinforced by
 *                            ALWAYS running li_crypt_verify() against
 *                            SOME hash (the real account's, or a fixed
 *                            dummy one for a nonexistent name) so a
 *                            timing difference doesn't leak which case
 *                            occurred either.
 *   LOGIN_NO_ACCOUNTS     -- pwpath doesn't exist at all -- the real
 *                            bootstrap case (no account has ever been
 *                            provisioned via pwtool on this image). NOT a
 *                            fail-open bug: the caller's own decision
 *                            about what to do with this code is what
 *                            determines behavior, and do_login() below
 *                            only ever uses it to preserve this project's
 *                            EXISTING, already-verified pre-Phase-5 boot
 *                            behavior (implicit root, no login prompt) --
 *                            it does not affect what happens once a real
 *                            account database DOES exist.
 *   LOGIN_AUTH_BROKEN     -- pwpath exists but failed to load/parse (real
 *                            corruption or I/O error) -- the caller must
 *                            NOT fall back to LOGIN_NO_ACCOUNTS-style
 *                            behavior for this case (that would be a real
 *                            fail-open: a damaged account database must
 *                            never silently grant an implicit root
 *                            shell). */
/* Real ENOENT value, cross-checked against
 * third_party/xnu-7195/bsd/sys/errno.h (`#define ENOENT 2`), not guessed --
 * matches the exact same distinction tools/pwtool/pwtool.c's own
 * pt_load_pw_or_bootstrap() already makes for the identical reason (see
 * that function's comment). */
#define LOGIN_ENOENT 2

static int attempt_login(const char *pwpath, const char *name, const char *password,
                          struct li_passwd **out_pw) {
    *out_pw = 0;

    /* REAL BUG FOUND AND FIXED DURING THIS PASS'S OWN HOST-TEST WRITING
     * (tools/init_binary/host_test.c's test #5): an earlier version of
     * this function treated ANY sys_open() failure -- not just ENOENT --
     * as LOGIN_NO_ACCOUNTS, meaning a real /etc/master.passwd that exists
     * but is unreadable for some other reason (permission bits corrupted,
     * a real I/O error, anything) would be silently treated as "no
     * account system provisioned yet" and fall back to an implicit root
     * shell -- a genuine fail-open bug, the exact class this whole file's
     * design is supposed to avoid. Fixed to check the real errno: only a
     * genuine ENOENT (the file truly doesn't exist) is the bootstrap
     * case; every other open failure is now LOGIN_AUTH_BROKEN (refuse ALL
     * logins), matching pwtool.c's own already-correct
     * pt_load_pw_or_bootstrap() logic exactly. */
    int fd = sys_open(pwpath, O_RDONLY, 0);
    if (fd < 0) {
        if (fd == -LOGIN_ENOENT) {
            return LOGIN_NO_ACCOUNTS;
        }
        return LOGIN_AUTH_BROKEN;
    }
    sys_close(fd);

    if (li_pw_load(pwpath) < 0) {
        return LOGIN_AUTH_BROKEN;
    }

    struct li_passwd *pw = li_getpwnam(name);
    /* Fixed dummy hash, real extended-format shape (so li_crypt() takes a
     * real code path, not an early-out on a malformed setting) -- used
     * only to equalize timing when `name` doesn't exist; can never
     * actually match any real password since it's not derived from one. */
    static const char *dummy_hash = "_J9..ZZZZXXXXXXXXXXX";
    const char *check_hash = pw ? pw->pw_passwd : dummy_hash;
    int match = li_crypt_verify(password, check_hash);

    if (!pw || !match) {
        return LOGIN_BAD_CREDENTIALS;
    }
    *out_pw = pw;
    return LOGIN_OK;
}

#define LOGIN_MAX_ATTEMPTS 5

/* Interactive login prompt loop -- real do_login() as opposed to
 * attempt_login()'s pure decision logic above. Returns 1 if the caller
 * should proceed into a shell (either a real authenticated session, or
 * the documented bootstrap fallback), 0 if the caller should give up
 * (single_user_child() responds to 0 by exiting so init_main()'s existing
 * respawn loop gives a fresh login prompt, same real shape getty/login
 * respawning has on real BSD). */
static int do_login(void) {
    for (int attempt = 0; attempt < LOGIN_MAX_ATTEMPTS; attempt++) {
        char name[64];
        char pass[PASSWORD_MAX];

        put("\nlogin: ");
        int n = read_line(name, sizeof(name));
        if (n <= 0) { attempt--; continue; } /* blank line -- don't burn an attempt */

        put("Password: ");
        int pn = read_line_noecho(pass, sizeof(pass));
        if (pn < 0) pass[0] = 0;

        struct li_passwd *pw = 0;
        int rc = attempt_login(MASTER_PASSWD_PATH, name, pass, &pw);
        scrub(pass, sizeof(pass)); /* real plaintext copy -- scrub immediately after use,
                                      regardless of outcome */

        if (rc == LOGIN_NO_ACCOUNTS) {
            put("init: no "); put(MASTER_PASSWD_PATH);
            put(" -- no accounts provisioned yet (run pwtool). "
                "Falling back to an implicit root shell.\n");
            current_uid = 0;
            current_gid = 0;
            kstrncpy(current_user, "root", sizeof(current_user));
            return 1;
        }
        if (rc == LOGIN_AUTH_BROKEN) {
            put("init: "); put(MASTER_PASSWD_PATH);
            put(" exists but failed to load -- refusing all logins "
                "(not falling back to an implicit shell for a damaged account database).\n");
            return 0;
        }
        if (rc == LOGIN_OK) {
            if (drop_privileges(pw->pw_uid, pw->pw_gid) != 0) {
                put("init: privilege drop failed -- aborting this session\n");
                return 0;
            }
            current_uid = pw->pw_uid;
            current_gid = pw->pw_gid;
            kstrncpy(current_user, pw->pw_name, sizeof(current_user));

            /* Best-effort cd to the account's real home directory --
             * validated via a real open()+fstat() first (same pattern
             * cmd_cd() already uses), falls back to "/" rather than
             * failing the whole login if pw_dir doesn't exist/isn't a
             * real directory. */
            char resolved[MAX_PATH];
            path_join("/", pw->pw_dir, resolved, sizeof(resolved));
            int dfd = sys_open(resolved, O_RDONLY, 0);
            if (dfd >= 0) {
                struct kstat st;
                long sr = sys_fstat(dfd, &st);
                sys_close(dfd);
                if (sr >= 0 && (st.st_mode & S_IFMT) == S_IFDIR) {
                    kstrncpy(cwd_path, resolved, MAX_PATH);
                }
            }

            put("\nWelcome, "); put(current_user); put(".\n");
            return 1;
        }

        put("\nLogin incorrect\n");
    }
    put("init: too many failed login attempts\n");
    return 0;
}


/* -------------------------------- builtins -------------------------------- */

static const char *prompt_name = "sh";

static void cmd_help(void) {
    put("available commands: ls cat cd pwd echo mkdir chown chmod help clear exit bash exec whoami su\n");
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

    /* Root is remounted read-write by remount_root_rw() at boot (see that
     * function) -- a failure here is real (most likely EACCES against a
     * directory this session doesn't have write permission on, e.g.
     * another account's home), not the stale "probably EROFS" this
     * message used to claim before that fix existed. */
    long r = sys_mkdir(resolved, 0755);
    if (r < 0) {
        put(argv[1]);
        put(": mkdir failed, rc="); putint(r); put("\n");
    }
}

static void cmd_clear(void) {
    put("\x1b[2J\x1b[H");
}

/* Same shape as tools/pwtool/pwtool.c's pt_parse_u32() -- this project's
 * established "small freestanding helpers get their own copy per
 * translation unit" convention, not shared via a header. */
static int parse_u32(const char *p, unsigned int *out) {
    unsigned long v = 0;
    const char *start = p;
    if (!p || !*p) return 0;
    while (*p) {
        if (*p < '0' || *p > '9') return 0;
        unsigned long digit = (unsigned long)(*p - '0');
        unsigned long nv = v * 10 + digit;
        if (nv < v || (nv - digit) / 10 != v) return 0;
        v = nv;
        p++;
    }
    if (v > 0xFFFFFFFFUL || p == start) return 0;
    *out = (unsigned int)v;
    return 1;
}

/* `chown <path> <uid> <gid>`: real chown(2), root-only enforcement is the
 * kernel's job (EPERM), not this file's -- provisioning helper for setting
 * up home directory ownership after pwtool useradd, since pwtool itself
 * only ever writes /etc/master.passwd entries, never touches the
 * filesystem (no notion of "home directory" as a real path there). */
static void cmd_chown(int argc, char **argv) {
    if (argc < 4) { put("usage: chown <path> <uid> <gid>\n"); return; }
    unsigned int uid, gid;
    if (!parse_u32(argv[2], &uid) || !parse_u32(argv[3], &gid)) {
        put("chown: bad uid/gid\n");
        return;
    }
    char resolved[MAX_PATH];
    path_join(cwd_path, argv[1], resolved, sizeof(resolved));
    long r = sys_chown(resolved, uid, gid);
    if (r < 0) {
        put(argv[1]); put(": chown failed\n");
    }
}

/* `chmod <path> <octal-mode>`: same rationale as cmd_chown() above. Mode
 * parsed as octal (real chmod(1) convention) via the same digit-loop shape
 * as parse_u32(), base 8 instead of base 10. */
static void cmd_chmod(int argc, char **argv) {
    if (argc < 3) { put("usage: chmod <octal-mode> <path>\n"); return; }
    unsigned int mode = 0;
    const char *p = argv[1];
    if (!*p) { put("chmod: bad mode\n"); return; }
    while (*p) {
        if (*p < '0' || *p > '7') { put("chmod: bad mode (octal only)\n"); return; }
        mode = mode * 8 + (unsigned int)(*p - '0');
        p++;
    }
    char resolved[MAX_PATH];
    path_join(cwd_path, argv[2], resolved, sizeof(resolved));
    long r = sys_chmod(resolved, (int)mode);
    if (r < 0) {
        put(argv[2]); put(": chmod failed\n");
    }
}

/* decimal, no libc -- needed to report a real child exit status/code from
 * cmd_exec() below (this codebase has no itoa/printf anywhere yet). */
static void putint(long v) {
    char buf[24];
    unsigned long uv;
    int neg = 0;
    if (v < 0) { neg = 1; uv = (unsigned long)(-v); } else { uv = (unsigned long)v; }
    int i = 0;
    if (uv == 0) buf[i++] = '0';
    while (uv) { buf[i++] = (char)('0' + (uv % 10)); uv /= 10; }
    if (neg) buf[i++] = '-';
    for (int a = 0, b = i - 1; a < b; a++, b--) {
        char t = buf[a]; buf[a] = buf[b]; buf[b] = t;
    }
    putn(buf, (size_t)i);
}

/* `exec <path> [args...]`: fork + execve + wait4 a real on-disk binary, for
 * driving tools/userland_staging's smoketests interactively from this
 * shell -- see docs/roadmap.md Phase 4. Real BSD wait status encoding
 * (bsd/sys/wait.h, unchanged since 4.4BSD): low 7 bits == 0 means the
 * child exited normally (as opposed to being signaled), in which case the
 * next byte up is the real exit() code -- decoded here rather than
 * printing the raw status, since that's what a caller actually wants to
 * know. */
static void cmd_exec(int argc, char **argv) {
    if (argc < 2) { put("usage: exec <path> [args...]\n"); return; }
    char resolved[MAX_PATH];
    path_join(cwd_path, argv[1], resolved, sizeof(resolved));

    int is_child = 0;
    long pid = sys_fork(&is_child);
    if (pid < 0) {
        put("exec: fork failed\n");
        return;
    }
    if (is_child) {
        static char *child_argv[MAX_ARGS + 1];
        int i;
        for (i = 0; i + 1 < argc && i < MAX_ARGS; i++) child_argv[i] = argv[i + 1];
        child_argv[i] = 0;
        static char *const envp0[] = { 0 };
        sys_execve(resolved, child_argv, envp0);
        /* only reached if execve() itself failed (e.g. ENOENT/ENOEXEC) */
        put(resolved);
        put(": exec failed\n");
        sys_exit(127);
    }

    long status = 0;
    sys_wait4((int)pid, &status, 0, 0);
    put("exec: "); put(resolved); put(" ");
    if ((status & 0x7f) == 0) {
        put("exited, status=");
        putint((status >> 8) & 0xff);
        put("\n");
    } else {
        put("terminated by signal ");
        putint(status & 0x7f);
        put("\n");
    }
}

static void cmd_whoami(void) {
    put(current_user); put(" (uid="); putint((long)current_uid);
    put(" gid="); putint((long)current_gid); put(")\n");

    /* Real integrity check, not just cosmetic: cross-checks this file's
     * own in-memory current_uid tracking against what the KERNEL actually
     * thinks this process's real uid is (a real getuid() syscall, not
     * trusting our own bookkeeping). These two should always agree --
     * current_uid is only ever set immediately after a real, checked-for-
     * success drop_privileges() call. A mismatch would mean either a real
     * bug in this file's tracking, or (far more concerning) that
     * drop_privileges() silently didn't take effect the way its own
     * success return value claimed -- exactly the "partial/ambiguous
     * privilege state" failure mode that function's own comment says must
     * never happen silently. */
    long real_uid = sys_getuid();
    if (real_uid >= 0 && (unsigned int)real_uid != current_uid) {
        put("whoami: WARNING -- kernel-reported uid ("); putint(real_uid);
        put(") does not match tracked current_uid ("); putint((long)current_uid);
        put(") -- this is a real bug, not cosmetic\n");
    }
}

/* Forward declaration -- real definition is down in the "shell" section
 * below (it depends on dispatch()/tokenize()/print_prompt(), all defined
 * later in this file); cmd_su() (immediately below) needs to call it from
 * its forked child before that point. */
void shell_loop(void);

/* Real su(1) semantics, via a real fork() -- NOT an in-place credential
 * mutation of this process. That distinction matters: this shell is one
 * long-running process (no per-command subshell the way a real dyld-linked
 * shell would spawn), so if `su` just called drop_privileges() directly on
 * the CURRENT process, it would be a ONE-WAY trip -- real setuid() to a
 * non-zero uid is permanent (see drop_privileges()'s own comment), so
 * there would be no way back to the original session without a fresh
 * login. Forking instead gives the real, expected behavior: the CHILD
 * process drops to the target account's credentials and runs its own
 * nested shell_loop() (real fork() gives it its own independent copy of
 * current_uid/current_gid/current_user -- mutating them in the child
 * never touches the parent's copy); when that nested shell's `exit`
 * terminates the child, sys_wait4() below unblocks and the ORIGINAL
 * parent shell resumes at its OWN original privilege level, completely
 * unaffected -- matching what real su(1) does by execve()-ing a fresh
 * shell process on top of a forked child.
 *
 * Real su(1) rule, replicated here: if the CALLER is already root
 * (current_uid == 0), no password is required to become anyone (root can
 * already do anything a password check would gate) -- otherwise the
 * TARGET account's real password is always required, regardless of who's
 * asking. */
static void cmd_su(int argc, char **argv) {
    const char *target_name = (argc > 1) ? argv[1] : "root";

    int fd = sys_open(MASTER_PASSWD_PATH, O_RDONLY, 0);
    if (fd < 0) { put("su: no accounts provisioned yet\n"); return; }
    sys_close(fd);
    if (li_pw_load(MASTER_PASSWD_PATH) < 0) { put("su: account database failed to load\n"); return; }

    struct li_passwd *target = li_getpwnam(target_name);
    if (!target) { put("su: unknown login\n"); return; }

    /* REAL STRUCTURAL LIMITATION, found via a genuine QEMU boot test of
     * this exact feature (not theorized in advance) and flagged here
     * rather than left as a confusing runtime failure: a non-root caller
     * can NEVER successfully setuid() to a DIFFERENT uid, no matter how
     * correct the password check above is -- real POSIX/BSD setuid()
     * semantics (unchanged, standard xnu behavior, nothing wrong with
     * this kernel) only let a non-privileged caller setuid() to its own
     * real/effective/saved uid, all three of which are already
     * current_uid after drop_privileges() ran (real setuid()-by-root sets
     * all three together, an irreversible drop -- see drop_privileges()'s
     * own comment). Real su(1) on real Unix systems works around this via
     * a SETUID-ROOT BINARY (mode 4755, owned by root) -- the KERNEL
     * grants root EUID at *execve() time* of such a binary regardless of
     * the calling process's own privilege, which is what actually lets a
     * real su prompt for a password and then genuinely become root. This
     * shell is one single, long-running, NON-setuid process (cmd_su()
     * forks and continues in the SAME image, it never execve()s a
     * separate on-disk binary), so that mechanism has no place to attach
     * -- a real, not-yet-built follow-up (a small separate setuid-root
     * helper binary at a fixed path) would be needed to close this gap,
     * out of scope for this pass. Checked BEFORE prompting for a
     * password (not after) so a real, correct password doesn't
     * misleadingly appear to have been rejected -- the actual failure
     * has nothing to do with what the user types. */
    if (current_uid != 0 && target->pw_uid != current_uid) {
        put("su: cannot become "); put(target_name);
        put(" from a non-root session -- this shell has no setuid-root "
            "helper binary yet (real kernel setuid() semantics only allow "
            "a non-root process to setuid() to its OWN uid; becoming a "
            "different account requires exec'ing a real setuid-root "
            "binary, not implemented in this build). su only works "
            "starting from root today.\n");
        return;
    }

    if (current_uid != 0) {
        char pass[PASSWORD_MAX];
        put("Password: ");
        int pn = read_line_noecho(pass, sizeof(pass));
        if (pn < 0) pass[0] = 0;
        int ok = li_crypt_verify(pass, target->pw_passwd);
        scrub(pass, sizeof(pass));
        if (!ok) { put("su: Sorry\n"); return; }
    }

    int is_child = 0;
    long pid = sys_fork(&is_child);
    if (pid < 0) { put("su: fork failed\n"); return; }

    if (is_child) {
        if (drop_privileges(target->pw_uid, target->pw_gid) != 0) {
            put("su: privilege drop failed\n");
            sys_exit(1);
        }
        current_uid = target->pw_uid;
        current_gid = target->pw_gid;
        kstrncpy(current_user, target->pw_name, sizeof(current_user));
        put("su: now "); put(current_user); put(" (type 'exit' to return)\n");
        shell_loop(); /* never returns -- 'exit' inside it calls sys_exit() directly */
        sys_exit(0);
    }

    long status = 0;
    sys_wait4((int)pid, &status, 0, 0);
    /* Parent's current_uid/current_gid/current_user were never touched --
     * separate address space since fork(), confirmed by construction, not
     * merely assumed. */
    (void)status;
}

/* ---------------------------------- shell ---------------------------------- */

static void print_prompt(void) {
    put(current_user);
    put("@");
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
    else if (kstreq(cmd, "chown")) cmd_chown(argc, argv);
    else if (kstreq(cmd, "chmod")) cmd_chmod(argc, argv);
    else if (kstreq(cmd, "clear")) cmd_clear();
    else if (kstreq(cmd, "exec")) cmd_exec(argc, argv);
    else if (kstreq(cmd, "whoami")) cmd_whoami();
    else if (kstreq(cmd, "su")) cmd_su(argc, argv);
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

/* Split out of what used to be shell_main() itself -- cmd_su() above needs
 * to run a nested read/dispatch loop (in its forked child, see cmd_su()'s
 * own comment) WITHOUT re-printing the top-level banner()
 * (that would be confusing -- "su" already prints its own one-line
 * "now <user>" message instead). shell_main() below is now just
 * banner() + shell_loop(), unchanged behavior for the normal (non-su)
 * case. */
void shell_loop(void) {
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

void shell_main(void) {
    banner();
    shell_loop();
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
    /* Phase 5: real login happens here, BEFORE anything else -- matches
     * real BSD ordering (login authenticates, THEN execs the target
     * shell/session, never the reverse). If do_login() fails (real bad
     * credentials after LOGIN_MAX_ATTEMPTS, or a damaged account
     * database -- see do_login()'s own comment for exactly which cases),
     * this process exits rather than falling through to a shell -- pid
     * 1's existing respawn loop (init_main() below) then forks a FRESH
     * child, giving a clean new login prompt with an empty fd table
     * again, same real shape getty/login respawning has. */
    if (!do_login()) {
        sys_exit(1);
    }

    static char *const argv0[] = { (char *)"/bin/sh", 0 };
    static char *const envp0[] = { 0 };
    sys_execve("/bin/sh", argv0, envp0);
    shell_main();
}

void init_main(void) {
    open_console();
    remount_root_rw();
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
