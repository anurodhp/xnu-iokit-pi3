/* Raw syscall wrappers real cron/crontab need that no prior tier had a
 * caller for. Same real carry-flag ABI / cerror_nocancel() convention as
 * libc_gen_syscalls.c/bash_syscalls.c (see those files' own header
 * comments for the full real-bug story). Syscall numbers cross-checked
 * directly against third_party/xnu-7195/bsd/kern/syscalls.master, not
 * guessed. fork()/pipe()/unlink()/wait4()+waitpid()/gettimeofday() below
 * are verbatim reuse of bash_syscalls.c's already-verified
 * implementations (not duplicated by accident -- this file can't just
 * #include that one, since its getpwnam()/getpwuid()/endpwent() are
 * honest "no lookup" stubs this project's real per-user crontab
 * execution needs the REAL implementation for instead, see
 * ps_passwd_compat.c/libinfo_custom.c). */

/* Real headers for size_t/ssize_t/pid_t/gid_t/uid_t/time_t/struct
 * sigaction/sigset_t, rather than this project's usual hand-rolled
 * typedefs (bash_syscalls.c's own `typedef long pid_t` pattern) --
 * needed here specifically because this file (unlike bash_syscalls.c)
 * also needs the real struct sigaction layout for _sigaction()/
 * __platform_sigaction() below to agree with what
 * libc_build_prototypes.h's forced pre-include and real signal.c/
 * sigaction.c already see; redeclaring pid_t etc by hand would conflict
 * with what these pull in transitively (found via a real compile error,
 * not guessed). */
#include <sys/types.h>
#include <signal.h>

extern long cerror_nocancel(int err);

static inline long csyscall3(long num, long a0, long a1, long a2) {
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
    if (is_error) return cerror_nocancel((int)x0);
    return x0;
}

/* #12 -- int chdir(user_addr_t path) */
int chdir(const char *path) {
    return (int)csyscall3(12, (long)path, 0, 0);
}

/* #136 -- int mkdir(user_addr_t path, int mode) */
int mkdir(const char *path, int mode) {
    return (int)csyscall3(136, (long)path, (long)mode, 0);
}

/* #90 -- int sys_dup2(u_int from, u_int to) -- public name is plain dup2 */
int dup2(int from, int to) {
    return (int)csyscall3(90, (long)from, (long)to, 0);
}

/* #147 -- int setsid(void) */
pid_t setsid(void) {
    return (pid_t)csyscall3(147, 0, 0, 0);
}

/* #181 -- int setgid(gid_t gid) */
int setgid(gid_t gid) {
    return (int)csyscall3(181, (long)gid, 0, 0);
}

/* #23 -- int setuid(uid_t uid) */
int setuid(uid_t uid) {
    return (int)csyscall3(23, (long)uid, 0, 0);
}

/* #183 -- int seteuid(uid_t euid) */
int seteuid(uid_t euid) {
    return (int)csyscall3(183, (long)euid, 0, 0);
}

/* #25 -- int geteuid(void) */
uid_t geteuid(void) {
    return (uid_t)csyscall3(25, 0, 0, 0);
}

/* #24 -- int getuid(void) */
uid_t getuid(void) {
    return (uid_t)csyscall3(24, 0, 0, 0);
}

/* #39 -- int getppid(void) */
pid_t getppid(void) {
    return (pid_t)csyscall3(39, 0, 0, 0);
}

/* #131 -- int sys_flock(int fd, int how) -- public name is plain flock */
int flock(int fd, int how) {
    return (int)csyscall3(131, (long)fd, (long)how, 0);
}

/* #201 -- int ftruncate(int fd, off_t length) */
int ftruncate(int fd, long length) {
    return (int)csyscall3(201, (long)fd, (long)length, 0);
}

/* #154 -- user_ssize_t pwrite(int fd, user_addr_t buf, user_size_t nbyte,
 * off_t offset) -- 4 args, needs its own trap helper. */
ssize_t pwrite(int fd, const void *buf, size_t nbyte, long offset) {
    register long x0 asm("x0") = fd;
    register long x1 asm("x1") = (long)buf;
    register long x2 asm("x2") = (long)nbyte;
    register long x3 asm("x3") = offset;
    register long x16 asm("x16") = 154;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x2), "r"(x3), "r"(x16)
                 : "memory", "cc");
    if (is_error) return (ssize_t)cerror_nocancel((int)x0);
    return (ssize_t)x0;
}

/* #243 -- int initgroups(u_int gidsetsize, gid_t *gidset, int gmuid) --
 * NO_SYSCALL_STUB per syscalls.master (Apple's own Perl generator skips
 * it), hand-written here same as libc_gen_syscalls.c/bash_syscalls.c
 * already do for their own NO_SYSCALL_STUB sets. Real public initgroups()
 * takes (const char *uname, int basegid) and resolves uname's full group
 * list itself on real macOS (via getgrouplist()/opendirectoryd) -- this
 * project has no group-enumeration API (same "no lookup API" gap
 * bash_syscalls.c's setpwent/getpwent stand-ins already document), so
 * this real syscall is invoked with just the one base gid as the whole
 * set, which is the correct, honest degraded behavior for a
 * single-group-per-user account model (matches this project's existing
 * flat-file resolver, which has never modeled supplementary groups). */
int initgroups(const char *uname, gid_t basegid) {
    (void)uname;
    gid_t gidset[1];
    gidset[0] = basegid;
    return (int)csyscall3(243, 1, (long)gidset, (long)basegid);
}

/* #50 -- int setlogin(char *namebuf) -- NO_SYSCALL_STUB, hand-written. */
int setlogin(const char *name) {
    return (int)csyscall3(50, (long)name, 0, 0);
}

/* #59 -- int execve(char *fname, char **argp, char **envp) -- real
 * Libc/gen/FreeBSD/exec.c's execl/execle/execlp/execv/execvp all call a
 * private `_execve()` (one leading underscore, same private-alias
 * convention documented in libc_build_prototypes.h/
 * simple_cmds_sigprocmask_compat.c's own header comments) rather than
 * the public name -- provided directly here since no separate
 * execve.c exists in this project's Libc pull. */
int _execve(const char *fname, char *const argv[], char *const envp[]) {
    return (int)csyscall3(59, (long)fname, (long)argv, (long)envp);
}

/* #48 -- int sigprocmask(int how, user_addr_t mask, user_addr_t omask) --
 * needed both directly (this file's own sigblock/sigsetmask below) and
 * as the real public symbol Libc/gen/FreeBSD/signal.c's real
 * bsd_signal()/sigset()/sigrelse() etc. resolve to; not pre-archived in
 * any of the libc_build archives (confirmed by nm), unlike most of libsyscall's
 * Perl-generated wrappers -- written directly here instead of chasing
 * the code-generation step for one symbol. */
int sigprocmask(int how, const sigset_t *set, sigset_t *oset) {
    return (int)csyscall3(48, (long)how, (long)set, (long)oset);
}

/* sigblock()/sigsetmask(): legacy 4.3BSD signal-mask calls, superseded
 * by sigprocmask(2) but still what cron/popen.c's real, unmodified
 * source calls directly (cron_pclose()'s SIGINT/SIGQUIT/SIGCHLD
 * blocking around waitpid). Real thin wrappers over sigprocmask, same
 * relationship real Apple's own (closed) implementation has -- not
 * reimplementing signal-mask semantics from scratch. Real SIG_BLOCK/
 * SIG_SETMASK come from <signal.h> (included above), not redefined here. */
int sigblock(int mask) {
    sigset_t old = 0, m = (sigset_t)mask;
    sigprocmask(SIG_BLOCK, &m, &old);
    return (int)old;
}
int sigsetmask(int mask) {
    sigset_t old = 0, m = (sigset_t)mask;
    sigprocmask(SIG_SETMASK, &m, &old);
    return (int)old;
}

/* #116 -- gettimeofday(struct timeval *tp, struct timezone *tzp) -- see
 * bash_syscalls.c's own header comment for why this is a plain raw
 * syscall here (no commpage fast path wired on this target). */
struct cron_timeval { long tv_sec; long tv_usec; };
int gettimeofday(struct cron_timeval *tp, void *tzp) {
    return (int)csyscall3(116, (long)tp, (long)tzp, 0);
}

/* time(2): real Apple implements this in terms of gettimeofday(), same
 * relationship kept here. */
time_t time(time_t *tp) {
    struct cron_timeval tv;
    gettimeofday(&tv, (void *)0);
    if (tp) {
        *tp = tv.tv_sec;
    }
    return tv.tv_sec;
}

/* gmtime_r(): the reentrant form real cron.c calls (cron_tick()); real,
 * unmodified UTC-only gmtime() already exists (ps_time_compat.c, see its
 * own header comment for the honest-UTC-only scope decision) -- this is
 * a thin wrapper copying its static result buffer into the caller's, the
 * real difference between the plain and _r forms. */
/* Field-for-field match with real Libc/include/time.h's struct tm
 * (including the two trailing fields plain 9-int structs would leave
 * uninitialized in the caller's real struct) -- an opaque local type
 * rather than #include <time.h> itself, to avoid the same real
 * pid_t-style typedef collision this file's own top comment already
 * documents for signal.h/sys/types.h. */
struct cron_tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst;
    long tm_gmtoff;
    char *tm_zone;
};
extern struct cron_tm *gmtime(const time_t *tp);
struct cron_tm *gmtime_r(const time_t *tp, struct cron_tm *result) {
    struct cron_tm *r = gmtime(tp);
    if (r == (struct cron_tm *)0) {
        return (struct cron_tm *)0;
    }
    *result = *r;
    return result;
}

/* #3 -- read(2) */
ssize_t read(int fd, void *buf, size_t n) {
    return (ssize_t)csyscall3(3, (long)fd, (long)buf, (long)n);
}

/* setprogname(): real Apple third_party/Csu/start.s/crt.c already
 * compute and set the real `__progname` global at process startup (see
 * crt_init_program_vars(), Phase 4.6 item 2/the __progname fix
 * documented in docs/roadmap.md's "Known bugs" section) -- this is the
 * real, matching setter do_command.c's child_process() calls after
 * fork() to relabel the running job for `ps`, not a separate mechanism. */
extern const char *__progname;
void setprogname(const char *name) {
    __progname = name;
}

/* #46 -- int sigaction(int signum, struct __sigaction *nsa, struct
 * sigaction *osa), NO_SYSCALL_STUB. Verbatim reuse of bash_syscalls.c's
 * already-verified __sigaction() raw syscall + real arm64 __sigtramp
 * signal-delivery trampoline (see that file's own header comment for the
 * exact real kernel register convention, cross-checked against
 * third_party/xnu-7195/bsd/dev/arm/unix_signal.c's
 * sendsig_set_thread_state64(), and its own documented scope: simple
 * (non-SA_SIGINFO) 1-arg handlers only, which is everything cron/popen.c
 * actually installs). Real Apple's public sigaction()/signal() chain is
 * sigaction() -> _sigaction_nobind()/__platform_sigaction() -> here;
 * this file compiles the real, unmodified
 * third_party/libplatform/src/setjmp/generic/sigaction.c for the
 * __platform_sigaction() half (see build_cron.sh) and provides
 * _sigaction_nobind() below as a direct real passthrough to it -- their
 * signatures are identical (confirmed by reading both), and real
 * Apple's own reason for the separate name is thread-registration nuance
 * this project's single-thread-per-process signal model doesn't need. */
extern int __sigaction(int sig, void *nsa, void *osa);
int __sigaction(int sig, void *nsa, void *osa) {
    return (int)csyscall3(46, (long)sig, (long)nsa, (long)osa);
}

/* Real signal.c's signal__() calls _sigaction() or _sigaction_nobind()
 * depending on a runtime `bind` flag under `#if defined(__DYNAMIC__)` --
 * that macro turns out to be predefined by this Apple-targeted compiler
 * regardless of whether this project actually has a dynamic linker (a
 * real correction to this file's first pass, which assumed it tracked
 * dyld presence and guessed _sigaction_nobind() was dead code; a real
 * undefined-symbol link error proved otherwise). Both real Apple
 * functions ultimately do the same job (install a handler + real
 * trampoline via __platform_sigaction()) -- the bind/nobind distinction
 * is about which thread's exception ports get touched, a real nuance
 * this project's single-thread-per-process signal model has no
 * observable difference for. Both provided as direct passthroughs. */
extern int __platform_sigaction(int sig, const struct sigaction *nsv,
    struct sigaction *osv);
int _sigaction(int sig, const struct sigaction *nsv, struct sigaction *osv) {
    return __platform_sigaction(sig, nsv, osv);
}
int _sigaction_nobind(int sig, const struct sigaction *nsv, struct sigaction *osv) {
    return __platform_sigaction(sig, nsv, osv);
}

asm(
    ".text\n"
    ".align 2\n"
    ".globl __sigtramp\n"
    "__sigtramp:\n"
    "  stp x19, x20, [sp, #-32]!\n"
    "  stp x21, x22, [sp, #16]\n"
    "  mov x19, x1\n"      /* infostyle */
    "  mov x20, x4\n"      /* uctx */
    "  mov x21, x5\n"      /* token */
    "  mov x22, x0\n"      /* catcher */
    "  mov x0, x2\n"       /* sig */
    "  mov x1, x3\n"       /* siginfo_t * (DAR-405: SA_SIGINFO handlers */
    "  mov x2, x4\n"       /* ucontext *   get (sig, info, uctx), as in */
    "  blr x22\n"          /* libplatform sigtramp.c; unix_signal.c:166-171) */
    "  mov x0, x20\n"      /* sigreturn(uctx, infostyle, token) */
    "  mov x1, x19\n"
    "  mov x2, x21\n"
    "  mov x16, #184\n"
    "  svc #0x80\n"
    "  brk #1\n"           /* sigreturn should never return */
);

/* endpwent(): real per-call-database teardown on real macOS. This
 * project's real getpwnam()/getgrnam() (ps_passwd_compat.c over
 * tools/libinfo_custom's flat-file resolver) do a fresh lookup every
 * call with no persistent database handle to close -- genuine no-op,
 * not a stand-in for a missing capability. */
void endpwent(void) {}

/* fork()/pipe()/unlink()/wait4()+waitpid(): verbatim reuse of
 * bash_syscalls.c's real, already-verified implementations (see that
 * file's own header comments for the full real-bug stories -- the
 * fork() child/parent x1-discriminator ABI quirk and pipe()'s
 * two-register return value in particular). Copied rather than linking
 * bash_syscalls.c itself, which this file's own top comment explains:
 * that file's getpwnam()/getpwuid()/endpwent() are honest "no lookup"
 * stubs, and this project's real per-user crontab execution needs the
 * REAL lookup this file gets from ps_passwd_compat.c instead -- linking
 * both would collide on those three symbols. */

/* fork() REMOVED (DAR-292): the canonical owner is libsystem_c.dylib
 * (real atfork handlers + _libc_fork_child()); cron/crontab bind it there.
 * tools/userland_staging/fork_owner_gate.sh enforces it. */

/* #42 -- real macOS ABI returns both fds via x0/x1 directly. */
int pipe(int fd[2]) {
    register long x0 asm("x0") = 0;
    register long x1 asm("x1") = 0;
    register long x16 asm("x16") = 42;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %2, cs"
                 : "+r"(x0), "+r"(x1), "=r"(is_error)
                 : "r"(x16)
                 : "memory", "cc");
    if (is_error) return (int)cerror_nocancel((int)x0);
    fd[0] = (int)x0;
    fd[1] = (int)x1;
    return 0;
}

/* #10 -- int unlink(user_addr_t path) */
int unlink(const char *path) {
    return (int)csyscall3(10, (long)path, 0, 0);
}

/* wait4()/waitpid() REMOVED (DAR-292): canonical owners are libsystem_c.dylib's
 * _wait4/_waitpid. wait() stays local -- no shipped dylib exports _wait --
 * and forwards to the canonical wait4(). */
extern pid_t wait4(pid_t pid, int *status, int options, void *rusage);
pid_t wait(int *status) {
    return wait4(-1, status, 0, (void *)0);
}
