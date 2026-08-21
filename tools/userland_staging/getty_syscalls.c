/* Real revoke(2)/setrlimit(2)/setsid(2) raw syscalls, a real select()
 * public-name wrapper, and an honest alarm()/dbopen()/pthread_testcancel()
 * stand-in -- Phase 5.5 item 1 (docs/roadmap.md), needed by real,
 * unmodified system_cmds/getty.tproj/{main,subr}.c, Libc/util/
 * login_tty.c, and Libc/gen/FreeBSD/getcap.c that no prior tier had
 * wired yet. Real syscall numbers all cross-checked against
 * third_party/xnu-7195/bsd/kern/syscalls.master, not guessed. Same
 * carry-flag raw-asm pattern every other hand-written NO_SYSCALL_STUB/
 * never-yet-generated shim in this project uses (cp_mv_syscalls.c,
 * simple_cmds_statfs64_compat.c, libc_gen_syscalls.c). chown(2)/dup2(2)/
 * execve(2)/access(2)/sigprocmask(2)/chmod(2) are NOT here -- all six
 * are already real, auto-generated raw stubs from build_simple_cmds.sh's
 * own SYS_STUBS_NEEDED batch (`$OUT/simple_obj/sys_*.o`), reused
 * directly at link time (build_getty.sh) rather than hand-duplicated.
 *
 * revoke (#56, plain) -- opentty()'s real vhangup-style "kick any other
 * opener off this tty" hygiene before (re)opening a serial line.
 * setrlimit (#195, NO_SYSCALL_STUB -- real Apple's public wrapper goes
 * through a private cancelable indirection this project hasn't built;
 * same "implement the real public symbol name directly" precedent as
 * cp_mv_syscalls.c) -- main.c's own GETTY_TIMEOUT/RLIM_INFINITY CPU-time
 * bracketing around the login-name prompt.
 * setsid (#147, plain) -- Libc/util/login_tty.c's real "become session
 * leader of the new controlling tty" step.
 */

typedef unsigned long size_t;

extern long cerror_nocancel(int err);

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

#define SYS_revoke    56
#define SYS_setrlimit 195
#define SYS_setsid    147

int revoke(const char *path) {
    return (int)syscall1(SYS_revoke, (long)path);
}

int setrlimit(unsigned int which, const void *rlp) {
    return (int)syscall3(SYS_setrlimit, which, (long)rlp, 0);
}

int setsid(void) {
    return (int)syscall1(SYS_setsid, 0);
}

/* pthread_testcancel(3): a real POSIX cancellation point -- "if this
 * thread has a pending cancellation request, terminate it now". This
 * project's libpthread port has never implemented real thread
 * cancellation at all (confirmed via nm: no such symbol anywhere in
 * libpthread.a, only callers) -- same class of gap as the various
 * no-op pthread_mutex_ and os_once copies docs/roadmap.md's Known
 * Stubbings already documents. Since cancellation requests are never
 * posted in the first place on this target, "never actually cancel" is
 * the exact correct behavior, not a degraded approximation. Real,
 * unmodified Libc/gen/FreeBSD/termios.c's real __tcdrain() (already
 * compiled into libc_gen.a by an earlier tier, pulled in here as a whole
 * archive member by getty's own tcgetattr()/tcsetattr() calls, even
 * though getty itself never calls tcdrain()) is this project's first
 * real reference to it. */
void pthread_testcancel(void) {
}

/* select(2): the SDK's <sys/_select.h> declares a real, unaliased-
 * looking "select" that in fact carries a trailing __DARWIN_1050(select)
 * attribute (confirmed by reading the header directly -- this project's
 * CFLAGS always define __DARWIN_VERS_1050=1, which resolves that macro's
 * asm-label suffix to "$1050"), so real call sites actually reference
 * `_select$1050`, not plain `_select` -- aliased to this wrapper by
 * build_getty.sh's own link step. Forwards to `_select` (the real raw
 * syscall libc_gen_syscalls.c already defines as C function `_select`,
 * part of libc_gen.a, first proven by bash's own build -- see
 * bash_syscalls.c's identical real gap/fix, reused verbatim: main.c's
 * real AC/answer-modem path -- dead code on this project's console-only
 * default gettytab, but still compiled -- references it). */
extern int _select(int nfds, void *readfds, void *writefds, void *exceptfds, void *timeout);
int select(int nfds, void *readfds, void *writefds, void *exceptfds, void *timeout) {
    return _select(nfds, readfds, writefds, exceptfds, timeout);
}

/* alarm(2): this project has NO real interval-timer/SIGALRM subsystem
 * anywhere (bash_syscalls.c's own alarm() is the exact same honest
 * no-op stub, see that file's header comment -- "a genuine, documented
 * behavioral gap... not fabricated support", tracked in
 * docs/roadmap.md's Known Stubbings). Reused verbatim rather than
 * attempting a real setitimer()-backed timer here -- that's a whole
 * separate, project-wide subsystem gap, out of scope for a single
 * command port. Real effect on getty: TO ("timeout") gettytab entries
 * and the SIGXCPU/RLIMIT_CPU-based hard timeout (main.c's own
 * setrlimit() call above, real) still bound a truly wedged login
 * prompt; only the softer, alarm()-based per-attempt nudge is a no-op. */
unsigned int alarm(unsigned int seconds) {
    (void)seconds;
    return 0;
}

/* dbopen(3): real Libc/gen/FreeBSD/getcap.c's gettable()/cgetent() path
 * tries a compiled Berkeley-DB-hashed ".db" version of /etc/gettytab
 * first (`dbopen(pbuf, O_RDONLY, 0, DB_HASH, 0)`), falling back to
 * plain-text parsing via a real, designed-in NULL check when that fails
 * (confirmed by reading getcap.c's own cgetent() directly -- the `else {
 * fd = _open(*db_p, ...); }` branch, not assumed) -- exactly the case
 * when no db(3)/ndbm library exists at all, which is also true on real
 * BSD systems that never ran cap_mkdb(1) against their gettytab. This
 * project has no Berkeley DB implementation (a real, separate,
 * genuinely-unbuilt subsystem -- same class of narrow, honest, always-
 * NULL/ENOSYS stand-in as cat_socket_stub.c's socket()/connect() for
 * text_cmds/cat's never-reached AF_UNIX path), so always failing here is
 * the SAME real behavior getty already has on a real system with a
 * plain-text-only /etc/gettytab -- not a degraded approximation. */
void *dbopen(const char *file, int flags, int mode, int type, const void *openinfo) {
    (void)file; (void)flags; (void)mode; (void)type; (void)openinfo;
    return (void *)0;
}
