/* Real socket/connect/bind/setsockopt/sendto/send/select raw syscall
 * wrappers -- res_send.c (third_party/libresolv, the only file in this
 * tier that touches sockets, confirmed by grep across every other
 * libresolv source file) is this project's first real consumer of the
 * actual DNS-over-UDP/TCP query path, needing real network syscalls
 * beyond the plain socket() ifconfig_extra_syscalls.c already added for
 * Phase 6b's ifconfig (that file's own header comment explains why this
 * project hand-rolls raw `svc #0x80` wrappers instead of Apple's real
 * Libsyscall stub generator -- same reasoning applies here, and same
 * "each consumer gets its own small duplicated wrapper file" convention
 * this project already uses for select() in bash_syscalls.c/
 * getty_syscalls.c/ncurses_extra_syscalls.c rather than a shared object).
 *
 * Every syscall number below is cross-checked against this project's own
 * kernel source, third_party/xnu-7195/bsd/kern/syscalls.master, not
 * guessed:
 *   #97  AUE_SOCKET     socket(domain, type, protocol)
 *   #98  AUE_CONNECT    connect(s, name, namelen)      NO_SYSCALL_STUB
 *   #104 AUE_BIND       bind(s, name, namelen)         NO_SYSCALL_STUB
 *   #105 AUE_SETSOCKOPT setsockopt(s, level, name, val, valsize)
 *   #133 AUE_SENDTO     sendto(s, buf, len, flags, to, tolen) NO_SYSCALL_STUB
 * (select(2), #93, already exists as _select() in libc_gen_syscalls.c /
 * libc_gen.a -- this file only adds the same public-name forwarder
 * every other consumer of it already duplicates locally.)
 *
 * send(2) has no dedicated kernel syscall on real Darwin either --
 * third_party/Libc/net/FreeBSD/send.c's real body is just
 * `return sendto(s, msg, len, flags, NULL, 0);` (via the
 * __sendto/__sendto_nocancel VARIANT_CANCELABLE aliasing that project's
 * real Libsyscall stub generator produces, which this build doesn't run
 * -- see ifconfig_extra_syscalls.c). Reproduced here as a thin real
 * wrapper over this file's own sendto(), same real semantics, no
 * Libsyscall indirection needed.
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

#define SYS_socket     97
#define SYS_connect    98
#define SYS_bind       104
#define SYS_setsockopt 105
#define SYS_sendto     133

/* socket()/connect()/send()/select() guarded out under
 * LIBRESOLV_SOCKET_SYSCALLS_DYLIB_VARIANT 2026-08-31 (static-linking
 * cleanup, dylib-vs-dylib duplicate-symbol pass): all four now have a
 * real, canonical home in libsystem_kernel.dylib (build_libsystem_
 * kernel.sh's own "BSD socket family" comment -- the exact same real
 * Apple source this project's other real consumer of these symbols,
 * libxpc.dylib's xpc_syscall_wrappers_compat.c/xpc_select_compat.c,
 * already proved compiles and works here), which build_libresolv_
 * dylib.sh already links as a sibling. `bind()`/`setsockopt()` are NOT
 * guarded -- libsystem_kernel.dylib doesn't have real copies of those
 * (yet), so this dylib's own real implementations stay the correct,
 * only copy. `sendto()` is ALSO NOT guarded, even though
 * libsystem_kernel.dylib now has a real copy too (kept private-extern
 * there specifically to avoid this exact conflict, see that script's
 * own comment): this dylib's own final link creates `_sendto$UNIX2003`
 * as a `-Wl,-alias` of this file's local `sendto()`, and per this
 * project's own documented ld64 finding an alias's base must be
 * genuinely local to the same link or ld64 silently emits a malformed
 * ordinal-0 re-export -- removing this local `sendto()` would break
 * that alias. `send()` has no such alias and calls this file's own
 * `sendto()` either way, so it's the one that moves.
 * Static-archive consumers of libresolv.a (dyld.macho via
 * build_dyld_link.sh, which must stay fully static; build_ping.sh;
 * build_corefoundation.sh) keep every one of these unchanged --
 * this guard only affects the dylib build's own recompiled variant,
 * see build_libresolv_dylib.sh's own comment. */
#ifndef LIBRESOLV_SOCKET_SYSCALLS_DYLIB_VARIANT
int socket(int domain, int type, int protocol) {
    return (int)syscall3(SYS_socket, domain, type, protocol);
}

int connect(int fd, const void *addr, unsigned int addrlen) {
    return (int)syscall3(SYS_connect, fd, (long)addr, addrlen);
}
#endif /* !LIBRESOLV_SOCKET_SYSCALLS_DYLIB_VARIANT */

int bind(int fd, const void *addr, unsigned int addrlen) {
    return (int)syscall3(SYS_bind, fd, (long)addr, addrlen);
}

int setsockopt(int fd, int level, int name, const void *val, unsigned int valsize) {
    return (int)syscall5(SYS_setsockopt, fd, level, name, (long)val, valsize);
}

ssize_t sendto(int fd, const void *buf, size_t len, int flags, const void *to, unsigned int tolen) {
    return syscall6(SYS_sendto, fd, (long)buf, (long)len, flags, (long)to, tolen);
}

#ifndef LIBRESOLV_SOCKET_SYSCALLS_DYLIB_VARIANT
ssize_t send(int fd, const void *buf, size_t len, int flags) {
    return sendto(fd, buf, len, flags, (const void *)0, 0);
}

/* select(2): public (non-underscore) name real res_send.c calls
 * verbatim, forwarding to the real _select() raw syscall already built
 * into libc_gen.a (libc_gen_syscalls.c) -- same trampoline every other
 * consumer (bash_syscalls.c, getty_syscalls.c, ncurses_extra_syscalls.c)
 * already duplicates locally rather than sharing one object. */
extern int _select(int nfds, void *readfds, void *writefds, void *exceptfds, void *timeout);
int select(int nfds, void *readfds, void *writefds, void *exceptfds, void *timeout) {
    return _select(nfds, readfds, writefds, exceptfds, timeout);
}
#endif /* !LIBRESOLV_SOCKET_SYSCALLS_DYLIB_VARIANT */
