/* Real chmod/getpeername/getsockname/recvmsg/writev/pselect raw syscall
 * wrappers -- real, project-wide gap (confirmed via `nm -gU` across
 * every sibling dylib this project has built so far: none export these
 * in any form, $UNIX2003-suffixed or plain), same class already flagged
 * in build_libresolv_dylib.sh's own header for the BSD-socket-syscall
 * wall (docs/backlog.md item d7's "BSD socket syscall wrappers in
 * userland libc" prerequisite). Deferred via `-Wl,-U` before, which
 * doesn't work: real dyld's eager chained-fixups binding fails at real
 * process load time regardless of whether the call path executes
 * (confirmed via a real QEMU boot of launchd). Real syscall numbers,
 * cross-checked against this project's own
 * third_party/xnu-7195/bsd/kern/syscalls.master, not guessed:
 *   #15  AUE_CHMOD       chmod(path, mode)               NO_SYSCALL_STUB
 *   #27  AUE_RECVMSG     recvmsg(s, msg, flags)          NO_SYSCALL_STUB
 *   #31  AUE_GETPEERNAME getpeername(fdes, asa, alen)    NO_SYSCALL_STUB
 *   #32  AUE_GETSOCKNAME getsockname(fdes, asa, alen)    NO_SYSCALL_STUB
 *   #121 AUE_WRITEV      writev(fd, iovp, iovcnt)
 *   #394 AUE_SELECT      pselect(nd, in, ou, ex, ts, mask)
 * Same hand-rolled `svc #0x80` pattern as every other raw syscall in
 * this project (see libresolv_socket_syscalls.c's own header for the
 * full "why not Apple's real Libsyscall stub generator" reasoning).
 * Real public names are exposed via `-Wl,-alias` in
 * build_libresolv_dylib.sh's own link (chmod/getpeername/getsockname/
 * recvmsg/writev all need a real $UNIX2003 alias; pselect needs $1050) --
 * this file only provides the real, unsuffixed raw syscall bodies.
 */
typedef unsigned long size_t;
typedef long ssize_t;

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

int chmod(const char *path, int mode) {
    return (int)syscall3(15, (long)path, mode, 0);
}

int getpeername(int fdes, void *asa, unsigned int *alen) {
    return (int)syscall3(31, fdes, (long)asa, (long)alen);
}

int getsockname(int fdes, void *asa, unsigned int *alen) {
    return (int)syscall3(32, fdes, (long)asa, (long)alen);
}

struct msghdr;
ssize_t recvmsg(int s, struct msghdr *msg, int flags) {
    return (ssize_t)syscall3(27, s, (long)msg, flags);
}

struct iovec;
ssize_t writev(int fd, const struct iovec *iovp, int iovcnt) {
    return (ssize_t)syscall3(121, fd, (long)iovp, iovcnt);
}

struct timespec;
struct sigset_pselect_compat;
int pselect(int nd, void *in, void *ou, void *ex, const struct timespec *ts, const void *mask) {
    return (int)syscall6(394, nd, (long)in, (long)ou, (long)ex, (long)ts, (long)mask);
}
