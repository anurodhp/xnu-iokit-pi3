/* Raw syscall wrappers for this Phase 4 gen/ tier (build_libc_gen.sh) -
 * real syscall numbers, each cross-checked against
 * third_party/xnu-7195/bsd/kern/syscalls.master the same way every other
 * raw syscall in this project is (see tools/init_binary/init.c's own
 * header comment for the carry-flag error-return ABI these all share).
 *
 * These particular syscalls are all marked NO_SYSCALL_STUB in
 * syscalls.master, meaning Apple's own Perl stub generator
 * (libsyscall/xcodescripts/create-syscalls.pl - already used for real in
 * build_libsyscall_smoketest.sh) deliberately does NOT produce an asm
 * stub for them; real Apple ships hand-written ones in libsyscall/custom/,
 * none of which cover these four (confirmed by find - genuinely absent
 * from this checkout, not overlooked). Hand-written here, same pattern
 * this project has used for every other syscall its own raw-syscall
 * layer needs (init.c, libc_locale_syscalls.c, libc_stdio_syscalls.c).
 */

typedef unsigned long size_t;
typedef long ssize_t;
typedef long off_t;

/* Real Apple convention (confirmed independently by this project's own
 * Perl-generated syscall stubs, e.g. libsyscall_smoketest's _mkdir.s:
 * `b.cc 2f / bl _cerror`): on the carry-flag error path, the raw x0
 * error code must be handed to cerror_nocancel() (real Apple errno.c
 * stand-in, cerror_stub.c - sets the global errno and returns -1), not
 * returned directly as a raw negative value. A prior version of this
 * file's syscall3/4/6 skipped that step entirely - every hand-written
 * wrapper here still correctly signaled "failure" (any nonzero/negative
 * return still fails a `!= 0`/`< 0` check), but never actually set
 * errno, so any real caller inspecting errno after a failed stat()/
 * lstat()/etc call (an extremely common real pattern) got a stale or
 * zero value instead of the real error code - found via a genuine QEMU
 * boot-test failure while building the removefile tier (a real,
 * unrelated caller's `errno != ENOENT` check misfired because errno had
 * never been set), not by inspection alone. */
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

static inline long syscall4(long num, long a0, long a1, long a2, long a3) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x3 asm("x3") = a3;
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x2), "r"(x3), "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        return cerror_nocancel((int)x0);
    }
    return x0;
}

static inline long syscall6(long num, long a0, long a1, long a2, long a3, long a4, long a5) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x3 asm("x3") = a3;
    register long x4 asm("x4") = a4;
    register long x5 asm("x5") = a5;
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        return cerror_nocancel((int)x0);
    }
    return x0;
}

/* #344 AUE_GETDIRENTRIES64 - third_party/Libc/gen/FreeBSD/telldir.h
 * declares this real Apple private name; real Libc source (opendir.c,
 * readdir.c) calls it directly whenever __DARWIN_64_BIT_INO_T is set
 * (this build's own CFLAGS already define that, matching the rest of
 * this project's Libc tiers). */
#define SYS_getdirentries64 344

size_t __getdirentries64(int fd, void *buf, size_t bufsize, off_t *basep) {
    long n = syscall4(SYS_getdirentries64, fd, (long)buf, (long)bufsize, (long)basep);
    return (size_t)n;
}

/* #463 AUE_OPENAT_RWTC - opendir.c's _filldir() reopens the directory via
 * openat(dirp->dd_fd, ".", ...) rather than plain open(), specifically so
 * the reopen can't race a concurrent chdir() the way a path-based open
 * could. Real kernel prototype takes `mode` unconditionally (not
 * variadic) - matches real sys_openat's own signature exactly. */
#define SYS_openat 463

int openat(int fd, const char *path, int flags, int mode) {
    return (int)syscall4(SYS_openat, fd, (long)path, flags, mode);
}

/* #346 AUE_FSTATFS64 - opendir.c's __fd_is_on_union_mount() calls the
 * public fstatfs(), which on real Darwin resolves (via the same kind of
 * $INODE64 symbol-suffix machinery build_libc_stdio.sh's own header
 * comment already found and deliberately avoided for fopen/fdopen) to
 * the real fstatfs64 syscall whenever __DARWIN_64_BIT_INO_T is set, which
 * this build already does - so this IS the real fstatfs64 struct
 * layout/ABI, not a 32-bit-inode compat path. `struct statfs64`'s real
 * field layout comes from the Xcode-12 iPhoneOS SDK's own
 * <sys/mount.h> (-isysroot already on this build's CFLAGS), not
 * redefined here. */
#define SYS_fstatfs64 346

int fstatfs(int fd, void *buf) {
    return (int)syscall3(SYS_fstatfs64, fd, (long)buf, 0);
}

/* #202 AUE_SYSCTL / #274 AUE_SYSCTL (sys_sysctlbyname) - real MIB-based
 * sysctl() (used by gen/FreeBSD/getpagesize.c's real CTL_HW/HW_PAGESIZE
 * lookup) and name-string-based sysctlbyname() (used by opendir.c's
 * __kernel_supports_unionfs() to read "kern.secure_kernel"). Real kernel
 * prototypes take separate `namelen`/`old`/`oldlenp`/`new`/`newlen`
 * arguments - more than arm64's 6 syscall-argument register budget
 * allows in one raw `svc`, so both wrappers here only pass what this
 * tier's real call sites actually use (both only ever read, never write,
 * and never set new/newlen) rather than building generic passthrough for
 * arguments nothing here exercises. sysctlbyname's real kernel signature
 * takes an explicit namelen (unlike the public sysctlbyname(3) API,
 * which computes it from strlen(name) - matches real Darwin's own
 * sysctlbyname.c behavior, not a shortcut). */
#define SYS_sysctl        202
#define SYS_sysctlbyname  274

extern size_t strlen(const char *);

int sysctl(int *name, unsigned int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
    /* Real kernel sysctl(2) takes all 6 args positionally in x0-x5;
     * this tier's real call sites (getpagesize.c) always pass
     * new=NULL/newlen=0 (read-only queries) - but a REAL bug was caught
     * here before landing: an earlier version of this wrapper used
     * syscall4 and silently dropped new/newlen from the actual svc call
     * entirely, leaving whatever was already in x4/x5 to be read by the
     * kernel as the "new value" pointer/length - a genuine
     * uninitialized-register hazard, not just unused C parameters.
     * Always forward the real values (0/0 for every current caller). */
    return (int)syscall6(SYS_sysctl, (long)name, namelen, (long)oldp, (long)oldlenp, (long)newp, (long)newlen);
}

int sysctlbyname(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
    (void)newp;
    (void)newlen;
    return (int)syscall6(SYS_sysctlbyname, (long)name, (long)strlen(name), (long)oldp, (long)oldlenp, 0, 0);
}

/* #338 AUE_STAT64 / #340 AUE_LSTAT64 - real syscall numbers, same
 * cross-check as everything else in this file. third_party/Libc/gen/
 * FreeBSD/getcwd.c's real directory-walking fallback path (used whenever
 * the fast __getcwd path this build doesn't implement isn't taken - see
 * this tier's own real _open/_fcntl/_fstat below) calls the PUBLIC
 * stat()/lstat()/fstat()/open()/fcntl() names directly, not the
 * underscore-prefixed private ones libc_stdio_syscalls.c/
 * libc_locale_syscalls.c already implement for OTHER real Libc source
 * files that call the private names instead - both are genuine, separate
 * public API surfaces on real Darwin (see libc_locale_syscalls.c's own
 * comment on the identical close()/_close() split), not a naming
 * inconsistency to paper over. Thin real aliases onto this project's
 * existing _open/_fcntl/_fstat, not reimplemented from scratch. */
extern int _open(const char *path, int flags, ...);
/* _fcntl deliberately NOT re-declared here (unlike _open/_fstat) - real
 * libc_stdio_syscalls.c's own definition is variadic (`int _fcntl(int,
 * int, ...)`, see that file's header comment for why), and
 * libc_build_prototypes.h's force-included `-include` already provides
 * a matching variadic declaration; a stale non-variadic redeclaration
 * used to live here (`extern int _fcntl(int fd, int cmd, long arg)`) and
 * silently never conflicted only because nothing had re-run this exact
 * build script since libc_build_prototypes.h grew its own (correct,
 * variadic) _fcntl entry for ps - caught for real 2026-08-19 as a build
 * failure the moment this file was rebuilt for the readline/termios
 * work, not guessed. */
extern int _fstat(int fd, void *statbuf);

int open(const char *path, int flags, ...) {
    /* Real open(2)'s mode argument is only meaningful with O_CREAT (same
     * reasoning as libc_locale_syscalls.c's _open) - reading it
     * unconditionally would read garbage for 2-arg call sites. */
    int mode = 0;
    if (flags & 0x00000200 /* O_CREAT, matches libc_locale_syscalls.c's OPEN_O_CREAT */) {
        __builtin_va_list ap;
        __builtin_va_start(ap, flags);
        mode = __builtin_va_arg(ap, int);
        __builtin_va_end(ap);
    }
    return _open(path, flags, mode);
}

int fcntl(int fd, int cmd, long arg) {
    return _fcntl(fd, cmd, arg);
}

int fstat(int fd, void *statbuf) {
    return _fstat(fd, statbuf);
}

#define SYS_stat64  338
#define SYS_lstat64 340

int stat(const char *path, void *statbuf) {
    return (int)syscall3(SYS_stat64, (long)path, (long)statbuf, 0);
}

int lstat(const char *path, void *statbuf) {
    return (int)syscall3(SYS_lstat64, (long)path, (long)statbuf, 0);
}

/* #54 AUE_IOCTL, NO_SYSCALL_STUB - real Apple hand-writes this in
 * libsyscall/wrappers/ioctl.c as a thin variadic-to-fixed-arity shim onto
 * a raw __ioctl(2) stub; this project's checkout has neither the
 * variadic wrapper nor the raw stub (confirmed absent by find, same gap
 * class as every other NO_SYSCALL_STUB entry in this file). Fixed
 * 3-arg form (matching every existing call site in this project's own
 * code, same shortcut tools/userland_staging/ps_extra_syscalls.c already
 * took for real ps -- ps.macho links and runs QEMU-verified with this
 * exact convention). _ioctl is the real underscore-prefixed private
 * alias third_party/Libc/gen/FreeBSD/termios.c calls directly (same
 * public/private split as open/_open, fcntl/_fcntl, fstat/_fstat above).
 *
 * Empirically confirmed real and reachable 2026-08-19 (not assumed): a
 * disposable ioctl(0, TIOCGETA/TIOCSETA, ...) probe built into
 * tools/init_binary/init.c (removed before commit) round-tripped real
 * termios state through /dev/console -- TIOCGETA returned the exact
 * TTYDEF_IFLAG/TTYDEF_OFLAG/(CREAD|CS8|CLOCAL)/TTYDEF_LFLAG default
 * values kmopen() (bsd/dev/arm/km.c) sets, and a TIOCSETA clearing
 * ICANON|ECHO (real raw-mode, VMIN=1/VTIME=0) both reported success and
 * produced a real, immediately-delivered, unechoed single-byte read()
 * back -- proving third_party/xnu-7195's real BSD tty/line-discipline
 * stack (km.c -> linesw -> tty.c's ttioctl_locked/ttread) is already
 * fully wired for this target, contrary to this project's own earlier,
 * untested assumption (see docs/roadmap.md) that no real tty existed
 * here. */
#define SYS_ioctl 54

int ioctl(int fd, unsigned long request, void *arg) {
    return (int)syscall3(SYS_ioctl, fd, (long)request, (long)arg);
}

int _ioctl(int fd, unsigned long request, void *arg) {
    return ioctl(fd, request, arg);
}

/* #121 AUE_WRITEV / #93 AUE_SELECT, both NO_SYSCALL_STUB - real
 * termios.c's tcdrain()/tcflush()/tcsendbreak() (whole-file pull-in the
 * moment anything in this .o needs tcgetattr/tcsetattr) reference
 * _writev/_select directly, same underscore-prefixed-private-alias
 * pattern as _ioctl above. Real signatures/numbers cross-checked the
 * same way as everything else here; this project's own
 * ps_extra_syscalls.c (build_ps.sh) independently proved this exact
 * pair real and correct first (ps.macho links+runs QEMU-verified with
 * it) - moved the canonical copy here once bash needed the same real
 * termios.c and ps_extra_syscalls.c's own copies were removed to avoid
 * a duplicate-symbol clash once both link libc_gen.a. */
#define SYS_writev 121
#define SYS_select 93

static inline long syscall5(long num, long a0, long a1, long a2, long a3, long a4) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x3 asm("x3") = a3;
    register long x4 asm("x4") = a4;
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        return cerror_nocancel((int)x0);
    }
    return x0;
}

int _writev(int fd, const void *iov, int iovcnt) {
    return (int)syscall3(SYS_writev, fd, (long)iov, iovcnt);
}

int _select(int nfds, void *readfds, void *writefds, void *exceptfds, void *timeout) {
    return (int)syscall5(SYS_select, nfds, (long)readfds, (long)writefds, (long)exceptfds,
                          (long)timeout);
}
