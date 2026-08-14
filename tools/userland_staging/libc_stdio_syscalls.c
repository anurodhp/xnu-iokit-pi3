/* Real, public-name lseek(2) - third_party/Libc/stdio/FreeBSD/stdio.c's
 * __sseek() (the low-level cookie callback fopen()-family FILEs install)
 * calls plain lseek(), not the underscore-prefixed private _lseek some
 * other Libc internals use. Same real syscall number/carry-flag ABI as
 * libc_locale_stdio.c's own internal sys_lseek() (cross-checked against
 * third_party/xnu-7195/bsd/kern/syscalls.master the same way every other
 * raw syscall in this project is) - duplicated here as the PUBLIC symbol
 * name real Libc source expects, rather than exporting that file's
 * static helper.
 */

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
        return -x0;
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
 * meaningful for a handful of cmds (F_SETFL, not used by this tier's real
 * call sites - they only pass F_GETFL) - reading it unconditionally as a
 * variadic arg the way _open's mode arg is handled would be wrong here
 * since F_GETFL callers genuinely pass no third argument at all; instead
 * this takes a fixed `long arg`, matching real sys_fcntl's own kernel
 * prototype exactly (see syscalls.master), which is safe for every cmd
 * this tier's real callers use. */
#define SYS_dup2  90
#define SYS_fcntl 92

int _dup2(int from, int to) {
    return (int)syscall3(SYS_dup2, from, to, 0);
}

int _fcntl(int fd, int cmd, long arg) {
    return (int)syscall3(SYS_fcntl, fd, cmd, arg);
}
