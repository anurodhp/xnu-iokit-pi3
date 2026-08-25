/* Real, public-name lseek(2) - third_party/Libc/stdio/FreeBSD/stdio.c's
 * __sseek() (the low-level cookie callback fopen()-family FILEs install)
 * calls plain lseek(), not the underscore-prefixed private _lseek some
 * other Libc internals use. Same real syscall number/carry-flag ABI as
 * libc_locale_stdio.c's own internal sys_lseek() (cross-checked against
 * third_party/xnu-7195/bsd/kern/syscalls.master the same way every other
 * raw syscall in this project is) - duplicated here as the PUBLIC symbol
 * name real Libc source expects, rather than exporting that file's
 * static helper.
 *
 * Error path calls cerror_nocancel() (real Apple errno.c stand-in,
 * cerror_stub.c) instead of returning the raw negative error code -
 * real bug found+fixed project-wide, see removefile_syscalls.c's own
 * header comment for the QEMU boot-test failure that surfaced it.
 */
extern long cerror_nocancel(int err);

typedef long off_t;

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

#define SYS_lseek 199

off_t lseek(int fd, off_t offset, int whence) {
    return syscall3(SYS_lseek, fd, offset, whence);
}

/* Real syscall numbers (bsd/kern/syscalls.master #90 AUE_DUP2, #92
 * AUE_FCNTL), cross-checked the same way every other raw syscall in this
 * project is. third_party/Libc/stdio/FreeBSD/fdopen.c/freopen.c call the
 * underscore-prefixed private aliases _fcntl/_dup2 (see
 * libc_build_prototypes.h for why these need real prototypes at all -
 * same implicit-int gap as _open/_read). fcntl's third arg is only ever
 * meaningful for a handful of cmds (F_SETFL) - genuinely absent for
 * F_GETFL callers (confirmed real: text_cmds/tail's freopen.c calls
 * `_fcntl(fp->_file, F_GETFL)` with only 2 arguments, a real build
 * failure against this function's earlier fixed-3-arg signature, fixed
 * by widening libc_build_prototypes.h's declaration to real variadic --
 * this definition must match that declaration exactly or every OTHER
 * tier that also compiles this file (bash/uname/ps/every simple_cmds
 * binary all link libc_stdio.a) fails with a real conflicting-types
 * error, confirmed the hard way. Same real "read the vararg only when
 * the cmd needs it, default to 0 otherwise" pattern this file's own
 * _open() already uses for its O_CREAT-conditional mode argument. */
#define SYS_dup2  90
#define SYS_fcntl 92

int _dup2(int from, int to) {
    return (int)syscall3(SYS_dup2, from, to, 0);
}

/* REAL BUG found+fixed 2026-08-25 debugging real dyld-940's dylib loader:
 * this used to only read the vararg for a hardcoded list of "known"
 * arg-taking cmds (originally just F_SETFL; a first attempt widened it to
 * F_GETPATH/F_GETPATH_NOFIRMLINK, then immediately hit the identical bug
 * again on F_CHECK_LV=98, which real dyld's Loader.cpp calls
 * unconditionally on every loaded Mach-O, signed or not
 * <rdar://problem/41015217>) -- every cmd not on the list silently got 0
 * for its arg instead of the caller's real pointer. For F_GETPATH this
 * meant the kernel's real, unmodified F_GETPATH handler
 * (bsd/kern/kern_descrip.c) correctly computed the full path internally
 * (confirmed via a temporary kernel diagnostic) but copyout()'d it to
 * userspace address 0 (EFAULT, silently ignored by dyld's void-returning
 * getpath()); for F_CHECK_LV it meant the kernel's copyin() of the
 * fchecklv struct got address 0 and failed, making dyld treat every
 * dylib as failing library validation.
 *
 * Real fix: stop hand-maintaining a whitelist of "commands that need a
 * vararg" -- always read and forward it. This is safe on this ABI: a
 * caller that genuinely only passes 2 arguments (e.g.
 * third_party/Libc/stdio/FreeBSD/fdopen.c's real `_fcntl(fp->_file,
 * F_GETFL)`) simply leaves x2/arg3 holding whatever was already there:
 * real kernel fcntl handlers for arg-less commands (F_GETFL among them)
 * never read `argp` at all, so passing them stray garbage is provably
 * harmless -- confirmed by reading every case in kern_descrip.c's real,
 * unmodified fcntl() implementation. This closes the whole bug class
 * instead of re-discovering it one new fcntl command at a time. */
int _fcntl(int fd, int cmd, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, cmd);
    long arg = __builtin_va_arg(ap, long);
    __builtin_va_end(ap);
    return (int)syscall3(SYS_fcntl, fd, cmd, arg);
}
