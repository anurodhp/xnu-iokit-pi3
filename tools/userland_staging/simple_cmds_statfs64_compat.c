/* Real statfs()/getfsstat() -- Phase 4.6 item 8's df(1)/du(1)/mv(1) MIB
 * work (docs/roadmap.md).
 *
 * This project's build_simple_cmds.sh previously generated a plain
 * "_statfs" raw stub via the standard SYS_STUBS_NEEDED/create-syscalls.pl
 * batch (same mechanism as _readlink/_chown/etc) and aliased it to
 * "_statfs$INODE64" (the symbol name real Apple headers rewrite every
 * `statfs()` call site to, via __DARWIN_INODE64(), whenever
 * __DARWIN_64_BIT_INO_T is set -- which this whole project's CFLAGS
 * always does). That auto-generated stub is backed by
 * bsd/kern/syscalls.master's #157 entry (`AUE_STATFS ... { int
 * statfs(char *path, struct statfs *buf); }`), which is the REAL but
 * legacy 32-bit-layout syscall (kernel's statfs()/vfs_syscalls.c fills
 * the old `short f_otype/f_oflags; long f_bsize/...` struct, not the
 * modern `struct statfs64` layout the SDK's own <sys/mount.h> puts
 * behind __DARWIN_64_BIT_INO_T). Any caller built with
 * __DARWIN_64_BIT_INO_T=1 (this whole project) that got the old #157
 * syscall wired to the new-struct-shaped "$INODE64" symbol would silently
 * misparse every statfs() result -- the exact same real
 * struct-size/layout mismatch this project's fstatfs() already had to
 * work around (see libc_gen_syscalls.c's own header comment) by using
 * SYS_fstatfs64 (#346) instead of the legacy SYS_fstatfs (#158). df(1)
 * is the first command that actually PRINTS statfs() field values (block
 * counts, free space) rather than just checking a return code, so this
 * mismatch would have been immediately visible as garbage -- caught here
 * before landing df, not discovered via a QEMU crash.
 *
 * Fix: hand-write real statfs()/getfsstat() via the correct 64-bit-struct
 * syscalls (#345 AUE_STATFS64 "statfs64", #347 AUE_GETFSSTAT64
 * "getfsstat64" -- both cross-checked against
 * third_party/xnu-7195/bsd/kern/syscalls.master, not guessed), same
 * carry-flag raw-asm pattern every other hand-written NO_SYSCALL_STUB-
 * style shim in this project uses (cp_mv_syscalls.c, removefile_syscalls.c,
 * libc_gen_syscalls.c's own fstatfs). Neither #345 nor #347 is actually
 * marked NO_SYSCALL_STUB in syscalls.master -- create-syscalls.pl COULD
 * generate stubs named "_statfs64"/"_getfsstat64" for them -- but this
 * project's established convention (see cp_mv_syscalls.c's header
 * comment) is to implement the real PUBLIC symbol name directly, so the
 * existing "-Wl,-alias,_statfs,_statfs$INODE64" (and this file's new
 * "_getfsstat$INODE64" analog) in build_simple_cmds.sh's shared
 * LINK_ALIASES continue to work unchanged, callers see the exact same
 * `statfs()`/`getfsstat()` C API real Libc source calls.
 */

typedef unsigned long size_t;

extern long cerror_nocancel(int err);

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
        return cerror_nocancel((int)x0);
    }
    return x0;
}

/* #345 AUE_STATFS64 - "int statfs64(char *path, struct statfs64 *buf)".
 * Real struct statfs64 layout comes from the Xcode-12 iPhoneOS SDK's own
 * <sys/mount.h> (already on every simple_cmds CFLAGS -isysroot), not
 * redefined here -- same approach libc_gen_syscalls.c's fstatfs() takes. */
#define SYS_statfs64 345

int statfs(const char *path, void *buf) {
    return (int)syscall3(SYS_statfs64, (long)path, (long)buf, 0);
}

/* #347 AUE_GETFSSTAT64 - "int getfsstat64(user_addr_t buf, int bufsize,
 * int flags)". Real Libc/gen/FreeBSD/getmntinfo.c (df(1)'s real
 * getmntinfo() -> getfsstat()) is this project's first real caller. */
#define SYS_getfsstat64 347

int getfsstat(void *buf, int bufsize, int flags) {
    return (int)syscall3(SYS_getfsstat64, (long)buf, (long)bufsize, (long)flags);
}
