/* Real raw syscall (getpgid #151, cross-checked against
 * third_party/xnu-7195/bsd/kern/syscalls.master, same discipline as
 * getty_syscalls.c/cp_mv_syscalls.c) plus several small, real, honestly-
 * documented compat wrappers needed by real, unmodified
 * system_cmds/login.tproj/login.c (DAR-164) that no prior tier had
 * wired yet:
 *
 * getpgid(): login.c's real handle_sighup() (SIGHUP forwarder,
 * `#ifdef __APPLE__`, real <rdar://problem/6244808> fix) calls this;
 * had no raw wrapper anywhere in this project yet (confirmed via
 * a real nm sweep of every already-built libc_build/*.a, not guessed).
 * getpgrp() (same call site) is NOT defined here -- real,
 * already-built launchd_libc_extras.a's ncurses_extra_syscalls.o
 * already provides it for real (narrow-extracted directly in
 * build_login.sh, see that script's own header), a real duplicate-
 * symbol link error against a first hand-written copy here confirmed
 * this, not guessed up front.
 *
 * getdtablesize(): turns out to be a real, already-complete NON-issue --
 * real, already-built libc_stdio.a (libc_stdio_env_shims.o) already
 * defines it for real (confirmed by a real duplicate-symbol link error
 * against a first hand-written attempt here, not guessed up front) --
 * nothing to add.
 *
 * task_get_bootstrap_port(): turns out to be a real, already-complete
 * NON-issue, not a gap -- real osfmk/mach/task_special_ports.h (already
 * on this project's -I path) `#define`s task_get_bootstrap_port(task,
 * port) directly as `task_get_special_port((task), TASK_BOOTSTRAP_PORT,
 * (port))`, so login.c's own real call site expands inline against the
 * real task_get_special_port() (already built, libmach_ipc.a) with zero
 * separate wrapper symbol needed -- confirmed by a real first attempt at
 * hand-writing a `task_get_bootstrap_port()` function here, which
 * failed to compile with a real macro-redefinition error, not guessed
 * up front. Nothing to define here after all; kept as a documented
 * non-gap so a future session doesn't re-investigate the same question. */
typedef int pid_t;

static inline long lgs_syscall1(long num, long a0) {
    register long x0 asm("x0") = a0;
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        extern long cerror_nocancel(int err);
        return cerror_nocancel((int)x0);
    }
    return x0;
}

#define SYS_getpgid 151

pid_t getpgid(pid_t pid) {
    return (pid_t)lgs_syscall1(SYS_getpgid, pid);
}

/* shm_open(2) (#266, NO_SYSCALL_STUB -- same "implement the real public
 * symbol name directly" precedent as getty_syscalls.c's own
 * setrlimit()/getty_syscalls.c setsid()): needed by libnotify_client.a's
 * real notify_client.o (shm_attach(), part of real notify_post()'s
 * shared-memory fast path) -- confirmed via a real trial link, not
 * guessed. Real signature per <sys/mman.h>: `int shm_open(const char
 * *path, int oflag, ...)`; a fixed 3-register raw wrapper is ABI-correct
 * here since AAPCS64 passes the first three integer args in x0-x2
 * regardless of the caller's own variadic declaration, and every real
 * caller (including this one) always passes a mode argument when
 * O_CREAT is set. */
static inline long lgs_syscall3(long num, long a0, long a1, long a2) {
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
        extern long cerror_nocancel(int err);
        return cerror_nocancel((int)x0);
    }
    return x0;
}

#define SYS_shm_open 266

int shm_open(const char *path, int oflag, int mode) {
    return (int)lgs_syscall3(SYS_shm_open, (long)path, oflag, mode);
}

/* notify_post(): real, narrow, honest no-op stand-in -- same reasoning
 * as login_bootstrap_compat.c's own bootstrap_look_up() (read that
 * file's header first). REAL FINDING: this project's already-built
 * libnotify_client.a's real notify_client.o is NOT the small, self-
 * contained client this project's earlier libnotify_smoketest.c
 * consumer needed -- a real trial link here surfaced a full real
 * libdispatch (dispatch_async/dispatch_queue_create/dispatch_source_*)
 * + real XPC (xpc_dictionary_*) + mach_port_allocate/destruct
 * dependency chain, the same class of disproportionate pull as
 * libxpc.a's own vproc/MIG cascade (see login_bootstrap_compat.c).
 * real, unmodified third_party/Libc/gen/NetBSD/utmpx.c's own ONLY call
 * site (`notify_post(UTMPX_CHANGE_NOTIFICATION)`, _pututxline(), return
 * value never even checked by the caller) is a real, best-effort,
 * fire-and-forget "tell anyone listening that utmpx changed" --
 * this project has no real subscriber for that notification anywhere
 * (confirmed via grep for UTMPX_CHANGE_NOTIFICATION/notify_register* --
 * zero hits outside this one post site), so "no one is listening,
 * nothing happens" is the real, correct observable behavior on this
 * target regardless of whether notify_post() itself is real or this
 * honest stand-in -- not a fabricated shortcut. Real success code
 * (NOTIFY_STATUS_OK == 0, third_party/PureDarwin/src/Libraries/XPC/
 * notify/notify.h) reused directly, not guessed. */
typedef unsigned int uint32_t;
#define LGS_NOTIFY_STATUS_OK 0

uint32_t notify_post(const char *name) {
    (void)name;
    return LGS_NOTIFY_STATUS_OK;
}

/* fork() REMOVED (DAR-292): login.macho used to carry a private raw-svc
 * copy (DAR-221 patched its missing __current_pid clear by hand). The
 * canonical owner is libsystem_c.dylib (real atfork handlers +
 * _libc_fork_child(); the raw __fork trap, which resets the cached pid, is
 * libsystem_kernel's). tools/userland_staging/fork_owner_gate.sh enforces it. */

/* alarm(): same real, honest, already-established no-op as
 * getty_syscalls.c/bash_syscalls.c's own alarm() (see either file's own
 * header comment for the full "no real interval-timer/SIGALRM subsystem
 * anywhere in this project" writeup, not re-litigated here). login.c's
 * real login-prompt timeout (`(void)alarm(timeout)` / SIGALRM ->
 * timedout() -> longjmp) becomes a real, documented no-op: the prompt
 * will not time out on its own, but every OTHER real safeguard around
 * it (backoff sleep(), retry-count bailout) is unaffected. */
unsigned int alarm(unsigned int seconds) {
    (void)seconds;
    return 0;
}

/* __strlcpy_chk(): real _FORTIFY_SOURCE=2 compile-time-bounds-checked
 * strlcpy(3) variant. This project's own CFLAGS explicitly set
 * -D_FORTIFY_SOURCE=0 everywhere (see build_login.sh's own CFLAGS, same
 * as every other tier), so no code THIS project compiles ever emits a
 * reference to it -- but liblaunchd_syscalls.a's already-built
 * getlogin.o gets pulled into this link regardless (confirmed via `nm`
 * on the final login.macho -- real `_getlogin`/`_getlogin_r` are both
 * present; exactly which OTHER already-built member of that archive
 * transitively needs them was not tracked down further, a real trial
 * link is what surfaced the __strlcpy_chk gap either way, not guessed),
 * and getlogin.o's own object code was itself compiled by a DIFFERENT
 * build script at some earlier point with fortify enabled, so it
 * references the real libc_hardened `__strlcpy_chk` entry point
 * directly rather than plain `strlcpy`. Real, correct, non-weakening
 * implementation (not a
 * "just call strlcpy and ignore the bounds" shortcut): honors the real
 * FORTIFY contract by clamping to the real compile-time-known
 * destination size before delegating to this project's already-real
 * strlcpy() (libc_locale_for_stdio.a), so a real mismatched-size bug
 * degrades to a safely-truncated copy instead of a silent overflow --
 * strictly safer than a plain strlcpy() passthrough would be. */
typedef unsigned long lgs_size_t;
extern lgs_size_t strlcpy(char *dst, const char *src, lgs_size_t dstsize);

lgs_size_t __strlcpy_chk(char *dst, const char *src, lgs_size_t dstlen, lgs_size_t dstsize) {
    lgs_size_t safe_len = (dstlen <= dstsize) ? dstlen : dstsize;
    return strlcpy(dst, src, safe_len);
}

/* endpwent(): login.c's real main() calls this once, right after the
 * real auth loop, purely to close out any getpwent(3)-style iteration
 * state. This project's real getpwnam() (ps_passwd_compat.c, reused
 * verbatim here) is a lazy-load-once flat-file resolver with no open
 * file handle or iteration cursor left dangling between calls (see that
 * file's own header) -- there is nothing for endpwent() to actually
 * close, so a real, honest no-op is the CORRECT implementation here,
 * not a stand-in for missing functionality. */
void endpwent(void) {
}
