/* Raw syscall wrappers real ping(8) needs that no prior tier provided.
 * Same real carry-flag ABI / cerror_nocancel() convention as every other
 * raw syscall in this project (see libc_gen_syscalls.c's header comment
 * for the full history). Numbers cross-checked directly against
 * third_party/xnu-7195/bsd/kern/syscalls.master, not guessed.
 *
 * recvmsg (#27, AUE_RECVMSG, NO_SYSCALL_STUB per syscalls.master line 72
 * -- Apple's real create-syscalls.pl generator skips it, same class of
 * gap as every other hand-written wrapper in this project) is the one
 * genuinely new syscall this tier needs: confirmed absent from every
 * other tools/userland_staging .c file before this one (grepped across
 * the whole directory). ping.c's own recvloop() is this project's first
 * real caller.
 *
 * setuid (#23, AUE_SETUID) already exists in cron_extra_syscalls.c, but
 * that file also defines getuid() (which this build gets instead from
 * ps_extra_syscalls.c, reused unmodified -- see build_ping.sh) plus a
 * large number of unrelated fork/exec/wait/pwrite/initgroups symbols
 * ping has no use for and that would need their own transitive deps
 * (gmtime, __platform_sigaction, etc) resolved just to link one
 * function. Small, duplicated wrapper instead, matching this project's
 * own established convention (see libresolv_socket_syscalls.c's header
 * comment: "each consumer gets its own small duplicated wrapper file
 * ... rather than a shared object").
 */
typedef unsigned long size_t;
typedef long ssize_t;
typedef unsigned int uid_t;

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

#define SYS_setuid     23
#define SYS_recvmsg    27
#define SYS_sendmsg    28
#define SYS_sigaction  46
#define SYS_sigprocmask 48

int setuid(uid_t uid) {
    return (int)syscall3(SYS_setuid, (long)uid, 0, 0);
}

ssize_t recvmsg(int s, void *msg, int flags) {
    return (ssize_t)syscall3(SYS_recvmsg, s, (long)msg, flags);
}

/* #28 AUE_SENDMSG sendmsg(s, caddr_t msg, flags), NO_SYSCALL_STUB --
 * pinger()'s -R/record-route path builds a real struct msghdr and calls
 * this directly (grep-confirmed sole real call site). Same 3-arg raw
 * shape as recvmsg() above. */
ssize_t sendmsg(int s, const void *msg, int flags) {
    return (ssize_t)syscall3(SYS_sendmsg, s, (long)msg, flags);
}

/* #48 AUE_NULL sigprocmask(how, user_addr_t mask, user_addr_t omask) --
 * real recvloop() setup blocks SIGINT/SIGQUIT/SIGALRM before installing
 * handlers. Verbatim-pattern reuse of cron_extra_syscalls.c/
 * ncurses_extra_syscalls.c's already-verified body (see
 * libresolv_socket_syscalls.c's header comment for this project's
 * "each consumer gets its own small duplicated wrapper" convention). */
typedef unsigned int sigset_t;
int sigprocmask(int how, const sigset_t *set, sigset_t *oset) {
    return (int)syscall3(SYS_sigprocmask, (long)how, (long)set, (long)oset);
}

/* usleep(3): real Apple Libc/gen/FreeBSD/usleep.c is a thin wrapper that
 * splits useconds into a timespec and calls _nanosleep() -- and real
 * Apple's nanosleep (third_party/Libc/gen/nanosleep.c:104) performs the
 * wait as
 *     SEMWAIT_SIGNAL(clock_sem, MACH_PORT_NULL, 1, 1, sec, nsec)
 * with a REAL semaphore as cond_sem (clock_sem, created at that same
 * file's line 55 via semaphore_create(mach_task_self(), &clock_sem,
 * SYNC_POLICY_FIFO, 0)) and MACH_PORT_NULL only as mutex_sem. This is
 * that same real wait, with usleep.c's own us->sec/nsec split inlined
 * rather than routing through a separate nanosleep symbol ping doesn't
 * otherwise need.
 *
 * FIXED 2026-09-15 (DAR-312, same root cause as DAR-309). This used to
 * pass 0 for BOTH cond_sem and mutex_sem, justified by a comment
 * calling that a "real, simpler equivalent" -- a claim inherited from
 * libmalloc_sleep_shim.c's own (now also corrected) comment. It was
 * false: 0 is not a valid semaphore port name, so the real kernel
 * failed the call INSTANTLY instead of waiting. port_name_to_semaphore()
 * returns KERN_INVALID_NAME for !MACH_PORT_VALID(0) (xnu-7195
 * osfmk/kern/ipc_sync.c:63) without ever reaching
 * semaphore_wait_internal(); reached from __semwait_signal_nocancel()
 * (bsd/kern/kern_sig.c:1232) -> semaphore_timedwait_trap_internal()
 * (osfmk/kern/sync_sema.c:945). Net effect: usleep() did not delay at
 * all, so ping's inter-packet pacing (-i / flood -f) was never applied.
 *
 * ETIMEDOUT is the NORMAL success return for a relative timed wait that
 * ran to completion -- real Apple's own nanosleep.c:106 treats it as
 * success, and so does this. */
#include <errno.h>
#include <mach/mach_types.h>

extern int __semwait_signal(int cond_sem, int mutex_sem, int timeout,
    int relative, long long tv_sec, int tv_nsec);
extern void mach_ipc_clock_init(void);
extern mach_port_t g_mach_ipc_clock_sem;

int usleep(unsigned int usec) {
    if (usec == 0) {
        return 0;
    }
    mach_ipc_clock_init();
    int ret = __semwait_signal(g_mach_ipc_clock_sem, MACH_PORT_NULL, 1, 1,
        (long long)(usec / 1000000), (int)((usec % 1000000) * 1000));
    if (ret < 0 && errno != ETIMEDOUT) {
        return -1;
    }
    return 0;
}

/* time(2): real commpage-backed on Darwin (see gettimeofday.c comment
 * elsewhere in this build) -- verbatim reuse of bash_syscalls.c's
 * already-verified gettimeofday()-based body. ping.tproj's own
 * gmt2local(0) (ecnprobe/gmt2local.c, real Apple source, compiled by
 * build_ping.sh -- only reached via the optional --prtime long option)
 * is this build's sole real caller. */
extern int gettimeofday(void *tp, void *tzp);
long time(long *tloc) {
    struct { long tv_sec; long tv_usec; } tv;
    gettimeofday(&tv, (void *)0);
    if (tloc) {
        *tloc = tv.tv_sec;
    }
    return tv.tv_sec;
}

/* __darwin_check_fd_set_overflow(3): real Apple hardening helper the
 * SDK's <sys/_types/_fd_def.h> FD_SET/FD_ISSET/FD_CLR macros call
 * unconditionally on a static link (no weak-symbol fallback available).
 * Verbatim reuse of bash_syscalls.c's already-verified real bounds-check
 * body -- main()'s real select()-based recvloop() (no -f/flood path)
 * calls FD_SET on a plain, FD_SETSIZE-bounded fd_set for the socket fd. */
#define PING_FD_SETSIZE 1024
int __darwin_check_fd_set_overflow(int fd, const void *fd_set_ptr, int unlimited) {
    (void)fd_set_ptr;
    if (unlimited) {
        return 1;
    }
    return fd >= 0 && fd < PING_FD_SETSIZE;
}

/* Real sigaction(2) working path -- verbatim reuse of
 * cron_extra_syscalls.c's already-verified __sigaction() raw syscall +
 * real arm64 __sigtramp signal-delivery trampoline (see that file's own
 * header comment for the exact real kernel register convention,
 * cross-checked against third_party/xnu-7195/bsd/dev/arm/
 * unix_signal.c's sendsig_set_thread_state64()). Duplicated here rather
 * than linking cron_extra_syscalls.c itself, for the same reason
 * setuid() above is duplicated rather than reused from there: that
 * file's own gettimeofday()/time()/gmtime_r() would collide with this
 * build's real third_party/Libc/sys/gettimeofday.c (see build_ping.sh).
 *
 * Unlike cron/bash (whose real call sites never call the PUBLIC
 * `sigaction()` name -- they only need the private _sigaction/
 * _sigaction_nobind aliases signal.c calls), ping.c's own real
 * recvloop() setup (lines ~948-964) calls plain `sigaction()` directly
 * for SIGINT/SIGQUIT/SIGINFO/SIGALRM -- this is the first real consumer
 * of the public name combined with the real working trampoline (as
 * opposed to cp_mv_syscalls.c's own public sigaction(), which is an
 * honest ABI-mismatched passthrough with no real trampoline -- see that
 * file's header comment -- not suitable here since ping's Ctrl-C/
 * Ctrl-\/SIGINFO summary-on-exit behavior needs real, working signal
 * delivery, not just a syscall that doesn't crash).
 */
struct sigaction;
extern int __platform_sigaction(int sig, const struct sigaction *nsv, struct sigaction *osv);

int __sigaction(int sig, void *nsa, void *osa) {
    return (int)syscall3(SYS_sigaction, sig, (long)nsa, (long)osa);
}

int sigaction(int sig, const struct sigaction *nsv, struct sigaction *osv) {
    return __platform_sigaction(sig, nsv, osv);
}
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
    "  blr x22\n"
    "  mov x0, x20\n"      /* sigreturn(uctx, infostyle, token) */
    "  mov x1, x19\n"
    "  mov x2, x21\n"
    "  mov x16, #184\n"
    "  svc #0x80\n"
    "  brk #1\n"           /* sigreturn should never return */
);
