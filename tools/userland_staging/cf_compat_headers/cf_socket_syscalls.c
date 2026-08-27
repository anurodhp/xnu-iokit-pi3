/* Real BSD socket raw syscall wrappers CFSocket.c/CFRunLoop.c call --
 * this project's established convention for these (see
 * ifconfig_extra_syscalls.c's own header comment, and
 * libresolv_socket_syscalls.c for the closest analog: each real
 * consumer gets its own small hand-rolled `svc #0x80` wrapper file
 * rather than a shared syscall archive/dylib, same as select() being
 * duplicated across bash_syscalls.c/getty_syscalls.c/
 * ncurses_extra_syscalls.c) -- this project doesn't run Apple's real
 * Libsyscall stub generator for the socket family (its real
 * MI_ENTRY_POINT/SYS.h macro assembly + VARIANT_CANCELABLE aliasing
 * needs machinery this checkout doesn't build).
 *
 * Every syscall number cross-checked against this project's own kernel
 * source, third_party/xnu-7195/bsd/kern/syscalls.master, not guessed:
 *   #31  AUE_GETPEERNAME  (not needed by this tier -- confirmed absent
 *                          from CF's own real trial-link undefined-
 *                          symbol list, unlike getsockname)
 *   #32  AUE_GETSOCKNAME  getsockname(fdes, asa, alen)
 *   #97  AUE_SOCKET       socket(domain, type, protocol)
 *   #104 AUE_BIND         bind(s, name, namelen)          NO_SYSCALL_STUB
 *   #105 AUE_SETSOCKOPT   setsockopt(s, level, name, val, valsize)
 *   #106 AUE_LISTEN       listen(s, backlog)               NO_SYSCALL_STUB
 *   #118 AUE_GETSOCKOPT   getsockopt(s, level, name, val, avalsize)
 *   #135 AUE_SOCKETPAIR   socketpair(domain, type, protocol, rsv) NO_SYSCALL_STUB
 *   #403 AUE_RECVFROM     recvfrom_nocancel(...)           NO_SYSCALL_STUB
 *   #404 AUE_ACCEPT       accept_nocancel(...)             NO_SYSCALL_STUB
 *   #409 AUE_CONNECT      connect_nocancel(...)            NO_SYSCALL_STUB
 *   #413 AUE_SENDTO       sendto_nocancel(...)              NO_SYSCALL_STUB
 *
 * accept/connect/recvfrom/sendto use the real _nocancel trap numbers
 * directly as their real, unmodified implementation -- matching real
 * Apple's own libsyscall/wrappers/legacy/{accept,connect,recvfrom,
 * sendto}.c, which each call __*_nocancel() directly too (this project
 * has no real pthread cancellation support to make the cancelable
 * variant meaningfully different anyway, same reasoning this project's
 * other syscall tiers already used for _nocancel forms). recv() has no
 * dedicated kernel syscall on real Darwin either -- real
 * third_party/Libc/net/FreeBSD/recv.c's body is just
 * `recvfrom(s, buf, len, flags, NULL, 0)` -- reproduced here as a thin
 * real wrapper over this file's own recvfrom(), same real semantics.
 */
#include <sys/types.h>
#include <sys/socket.h>

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

#define SYS_getsockname     32
#define SYS_socket          97
#define SYS_bind            104
#define SYS_setsockopt      105
#define SYS_listen          106
#define SYS_getsockopt      118
#define SYS_socketpair      135
#define SYS_recvfrom_nocancel 403
#define SYS_accept_nocancel   404
#define SYS_connect_nocancel  409
#define SYS_sendto_nocancel   413

int socket(int domain, int type, int protocol) {
    return (int)syscall3(SYS_socket, domain, type, protocol);
}

int bind(int s, const struct sockaddr *name, socklen_t namelen) {
    return (int)syscall3(SYS_bind, s, (long)name, namelen);
}

int listen(int s, int backlog) {
    return (int)syscall3(SYS_listen, s, backlog, 0);
}

int accept(int s, struct sockaddr *addr, socklen_t *addrlen) {
    return (int)syscall3(SYS_accept_nocancel, s, (long)addr, (long)addrlen);
}

int connect(int s, const struct sockaddr *name, socklen_t namelen) {
    return (int)syscall3(SYS_connect_nocancel, s, (long)name, namelen);
}

int getsockname(int fdes, struct sockaddr * __restrict asa, socklen_t * __restrict alen) {
    return (int)syscall3(SYS_getsockname, fdes, (long)asa, (long)alen);
}

int getsockopt(int s, int level, int name, void * __restrict val, socklen_t * __restrict avalsize) {
    return (int)syscall5(SYS_getsockopt, s, level, name, (long)val, (long)avalsize);
}

int setsockopt(int s, int level, int name, const void *val, socklen_t valsize) {
    return (int)syscall5(SYS_setsockopt, s, level, name, (long)val, valsize);
}

int socketpair(int domain, int type, int protocol, int sv[2]) {
    return (int)syscall4(SYS_socketpair, domain, type, protocol, (long)sv);
}

ssize_t recvfrom(int s, void * __restrict buf, size_t len, int flags,
                  struct sockaddr * __restrict from, socklen_t * __restrict fromlen) {
    return (ssize_t)syscall6(SYS_recvfrom_nocancel, s, (long)buf, (long)len, flags, (long)from, (long)fromlen);
}

ssize_t sendto(int s, const void *msg, size_t len, int flags,
                const struct sockaddr *to, socklen_t tolen) {
    return (ssize_t)syscall6(SYS_sendto_nocancel, s, (long)msg, (long)len, flags, (long)to, tolen);
}

ssize_t recv(int s, void *buf, size_t len, int flags) {
    return recvfrom(s, buf, len, flags, (struct sockaddr *)0, (socklen_t *)0);
}
