/* Raw syscall wrappers for the sshd port (build_sshd.sh), DAR-183.
 *
 * Every syscall here is marked NO_SYSCALL_STUB in
 * third_party/xnu-7195/bsd/kern/syscalls.master, meaning Apple's own
 * stub generator (libsyscall/xcodescripts/create-syscalls.pl -- used by
 * this same build script for the dozen PLAIN syscalls sshd also needs)
 * deliberately emits no asm stub for them, and this checkout's
 * libsyscall/custom/ has no hand-written replacement either. Each number
 * below was read out of syscalls.master directly, with the line quoted
 * beside it -- not guessed, not carried over from another platform.
 *
 * Same hand-written shape (and the same real cerror_nocancel() carry-flag
 * error ABI) as this project's existing libc_gen_syscalls.c /
 * removefile_syscalls.c -- see libc_gen_syscalls.c's own header for why
 * the cerror step is load-bearing: skipping it still signals failure to
 * the caller but leaves errno stale, which really did break a caller's
 * `errno != ENOENT` check once already.
 *
 * NOT included here, deliberately, because a real one already exists and
 * is reused on the link line instead: getrlimit (NO_SYSCALL_STUB #194,
 * xargs_getrlimit_compat.c).
 */

typedef unsigned long size_t;
typedef unsigned int socklen_t;

extern long cerror_nocancel(int err);

static inline long syscall2(long num, long a0, long a1) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        return cerror_nocancel((int)x0);
    }
    return x0;
}

static inline long syscall1(long num, long a0) {
    register long x0 asm("x0") = a0;
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        return cerror_nocancel((int)x0);
    }
    return x0;
}

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

/* 30 AUE_ACCEPT ALL { int accept(int s, caddr_t name, socklen_t *anamelen)
 *                     NO_SYSCALL_STUB; }
 * sshd.c's server_accept_loop() and channels.c's four
 * channel_post_*_listener() handlers. */
#define SYS_accept 30
int accept(int s, void *name, socklen_t *anamelen) {
    return (int)syscall3(SYS_accept, s, (long)name, (long)anamelen);
}

/* 106 AUE_LISTEN ALL { int listen(int s, int backlog) NO_SYSCALL_STUB; }
 * sshd.c's listen_on_addrs(), channels.c's remote-forward listeners. */
#define SYS_listen 106
int listen(int s, int backlog) {
    return (int)syscall2(SYS_listen, s, backlog);
}

/* 135 AUE_SOCKETPAIR ALL { int socketpair(int domain, int type,
 *                          int protocol, int *rsv) NO_SYSCALL_STUB; }
 * monitor.c's privsep monitor<->child socket pair, and session.c. */
#define SYS_socketpair 135
int socketpair(int domain, int type, int protocol, int *rsv) {
    return (int)syscall4(SYS_socketpair, domain, type, protocol, (long)rsv);
}

/* 50 AUE_SETLOGIN ALL { int setlogin(char *namebuf) NO_SYSCALL_STUB; }
 * session.c's do_setusercontext() (real BSD login-name-in-the-kernel
 * setting, the same call real login(1) makes -- this project's own
 * login.macho reaches it through liblaunchd_syscalls.a's copy). */
#define SYS_setlogin 50
int setlogin(const char *namebuf) {
    return (int)syscall1(SYS_setlogin, (long)namebuf);
}

/* 124 AUE_FCHMOD ALL { int fchmod(int fd, int mode) NO_SYSCALL_STUB; }
 * sftp-server.c's process_fsetstat(), sshpty.c's pty_setowner(). */
#define SYS_fchmod 124
int fchmod(int fd, int mode) {
    return (int)syscall2(SYS_fchmod, fd, mode);
}

/* 128 AUE_RENAME ALL { int rename(char *from, char *to) NO_SYSCALL_STUB; }
 * sftp-server.c's process_rename(). (removefile_syscalls.c has its own
 * copy of this same wrapper for the removefile tier; it is not linked
 * here -- that archive brings a whole unrelated object with it.) */
#define SYS_rename 128
int rename(const char *from, const char *to) {
    return (int)syscall2(SYS_rename, (long)from, (long)to);
}

/* 345 AUE_STATFS64 ALL { int statfs64(char *path, struct statfs64 *buf); }
 * openbsd-compat/bsd-statvfs.c's statvfs()/fstatvfs() (this build has no
 * real statvfs -- see build_sshd.sh's config.h override table).
 *
 * Hand-written even though statfs IS a plain syscalls.master entry
 * (#157): Apple's create-syscalls.pl emits an EMPTY _statfs.s for this
 * target -- verified, the generated object contained nothing but ltmp0 --
 * because the 64-bit-inode ABI routes plain statfs() to the statfs64
 * trap instead. This target is __DARWIN_ONLY_64_BIT_INO_T (the iPhoneOS
 * SDK's own sys/cdefs.h:489 `#define __DARWIN_ONLY_64_BIT_INO_T 1`), so
 * `struct statfs` as these sources see it IS the kernel's struct
 * statfs64 layout, and #345 is the correct and only trap for it.
 */
#define SYS_statfs64 345
int statfs(const char *path, void *buf) {
    return (int)syscall2(SYS_statfs64, (long)path, (long)buf);
}

/* 220 AUE_GETATTRLIST ALL { int getattrlist(const char *path,
 *     struct attrlist *alist, void *attributeBuffer, size_t bufferSize,
 *     u_long options) NO_SYSCALL_STUB; }
 * Apple's own Libc realpath.c/getcwd.c use it to resolve a path's real
 * name; libcopyfile.dylib exports a copy, but that dylib is not its
 * owner and is not otherwise on sshd's link line. */
#define SYS_getattrlist 220
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
int getattrlist(const char *path, void *alist, void *attributeBuffer,
                size_t bufferSize, unsigned long options) {
    return (int)syscall5(SYS_getattrlist, (long)path, (long)alist,
                         (long)attributeBuffer, (long)bufferSize, (long)options);
}

/* 122 AUE_SETTIMEOFDAY ALL { int settimeofday(struct timeval *tv,
 *     struct timezone *tzp) NO_SYSCALL_STUB; }
 * Not called by sshd itself -- it comes in with Apple's real
 * clock_gettime.c, which defines clock_settime() in the same translation
 * unit as the clock_gettime() misc.c's monotime() needs. Real wrapper
 * rather than a stub so that TU is honestly complete. */
#define SYS_settimeofday 122
int settimeofday(const void *tv, const void *tzp) {
    return (int)syscall2(SYS_settimeofday, (long)tv, (long)tzp);
}

/* 244 AUE_POSIX_SPAWN ALL { int posix_spawn(pid_t *pid, const char *path,
 *     const struct _posix_spawn_args_desc *adesc, char **argv,
 *     char **envp) NO_SYSCALL_STUB; }
 * The raw trap behind Apple's real libsyscall posix_spawn.c (compiled
 * into this binary for Apple's popen.c, session.c's X11 xauth path).
 * That file calls the double-underscore name directly. */
#define SYS_posix_spawn 244
int __posix_spawn(int *pid, const char *path, const void *adesc,
                  char *const argv[], char *const envp[]) {
    return (int)syscall5(SYS_posix_spawn, (long)pid, (long)path, (long)adesc,
                         (long)argv, (long)envp);
}

/* __unix_conforming: a real Apple Libc global. Apple's own real
 * stdlib/FreeBSD/system.c reads it (its line 83) to pick UNIX03 vs
 * legacy-BSD signal semantics, and crt0 normally sets it to 1 the first
 * time a UNIX03-conforming entry point runs.
 *
 * Pre-set to 1 here for the same real reason (and with the same
 * reasoning) as this project's existing apply_sigsuspend_compat.c: every
 * tier of this build compiles with -D__DARWIN_UNIX03=1 unconditionally,
 * so "always conforming" is the exact, permanent answer, not a
 * placeholder standing in for crt0's lazy dance. Defined here rather
 * than linking that file's object, which also carries an unrelated
 * sigsuspend()/pthread_testcancel() pair this binary does not want.
 */
int __unix_conforming = 1;
