/* Raw syscall shims for the private low-level names (__open/__read/etc)
 * real Libc's locale-file-loading code calls internally (third_party/Libc/
 * locale/FreeBSD/{collate,ldpart,rune,setlocale}.c) - real syscall numbers,
 * cross-checked against third_party/xnu-7195/bsd/kern/syscalls.master the
 * same way tools/init_binary/init.c's raw syscalls are, not guessed.
 * Carry-flag errno convention (see init.c's own header comment for the
 * real root cause) applies here too.
 */

typedef unsigned long size_t;
typedef long ssize_t;
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

#define SYS_exit  1
#define SYS_read  3
#define SYS_write 4
#define SYS_open  5
#define SYS_close 6

/* Real open(2)'s mode argument is variadic (only meaningful with
 * O_CREAT) - a fixed 3-required-args signature rejected real call sites
 * in third_party/Libc/locale/FreeBSD/setlocale.c that pass just
 * (path, flags), caught once libc_build_prototypes.h gave the compiler
 * a real prototype to check against instead of an implicit-int guess.
 * Only reads the variadic arg when O_CREAT is actually set - reading it
 * unconditionally would be undefined behavior (garbage) for 2-arg call
 * sites that genuinely never pass a third argument at all, matching
 * real Darwin open(2)'s own documented/implemented behavior. */
#define OPEN_O_CREAT 0x00000200

int _open(const char *path, int flags, ...) {
    int mode = 0;
    if (flags & OPEN_O_CREAT) {
        __builtin_va_list ap;
        __builtin_va_start(ap, flags);
        mode = __builtin_va_arg(ap, int);
        __builtin_va_end(ap);
    }
    return (int)syscall3(SYS_open, (long)path, flags, mode);
}

ssize_t _read(int fd, void *buf, size_t n) {
    return syscall3(SYS_read, fd, (long)buf, (long)n);
}

ssize_t _write(int fd, const void *buf, size_t n) {
    return syscall3(SYS_write, fd, (long)buf, (long)n);
}

int _close(int fd) {
    return (int)syscall3(SYS_close, fd, 0, 0);
}

/* Public (non-underscore) alias - third_party/Libc/locale/FreeBSD/collate.c
 * calls plain close(), not the private _close() - both real, separate
 * public API surfaces on real Darwin, found needed when the linker
 * flagged "_close" (nm/ld's display for the C symbol `close`) as
 * unresolved with "(maybe you meant: __close)" (its display for the
 * already-defined `_close`). */
int close(int fd) {
    return _close(fd);
}

/* fstat/fstat64 split (bsd/kern/syscalls.master #189 plain vs #339 _64) -
 * same real bug class tools/init_binary/init.c's own header comment
 * documents finding for fstat/getdirentries (plain numbers fill an older/
 * different struct layout than modern callers expect) - use the 64 number
 * directly since real Libc's <sys/stat.h> struct stat here is the 64-bit-
 * ino layout. */
#define SYS_fstat64 339

int _fstat(int fd, void *statbuf) {
    return (int)syscall3(SYS_fstat64, fd, (long)statbuf, 0);
}
