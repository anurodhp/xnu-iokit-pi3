/* Raw syscall / compat shims real ncurses (third_party/ncurses, tag
 * ncurses-57) needs that no earlier tier's curated libc archives
 * (libc_build/libc_gen.a, libc_string.a, libc_locale.a, ...) export yet.
 * Same real carry-flag ABI / cerror_nocancel() error-reporting convention
 * every other *_extra_syscalls.c file in this directory uses (see
 * libc_locale_syscalls.c's own header comment for the full real-bug
 * story behind cerror_nocancel() vs a raw negative return). Syscall
 * numbers cross-checked directly against
 * third_party/xnu-7195/bsd/kern/syscalls.master, not guessed.
 *
 * lib_tstp.c (real ncurses/tty/lib_tstp.c, SIGINT/SIGTERM/SIGWINCH/
 * SIGTSTP handler installation + terminal-state cleanup on signal) is
 * NOT a self-registering plugin like ifconfig's af_inet6.c -- its
 * _nc_signal_handler() entry point is called unconditionally from
 * ncurses/tty/tty_update.c and ncurses/base/lib_newterm.c, so dropping
 * the file would just trade one real gap for a new undefined-symbol
 * gap. Real sigaction()/sigprocmask()/getpgrp() ARE already
 * genuinely available on this target -- this project's bash and cron
 * ports (tools/userland_staging/bash_syscalls.c,
 * tools/userland_staging/cron_extra_syscalls.c) already built and
 * QEMU-verified the exact same real chain (sigaction() ->
 * __platform_sigaction() [third_party/libplatform/src/setjmp/generic/
 * sigaction.c, real unmodified source] -> __sigaction() raw syscall,
 * with a real _sigtramp signal-delivery trampoline whose register
 * convention was read directly out of this project's own kernel source,
 * third_party/xnu-7195/bsd/dev/arm/unix_signal.c's
 * sendsig_set_thread_state64()) -- so sigaction/__sigaction/__sigtramp
 * and sigprocmask below are verbatim reuse of those two files'
 * already-verified bodies (cited per-block), not reinvented. This file
 * exists (rather than just linking bash_syscalls.o/cron_extra_syscalls.o
 * directly) because both of those files also define OTHER symbols
 * (gettimeofday, access, getpwnam, ...) this build already gets from
 * elsewhere or doesn't want duplicate-defined -- same reasoning
 * cron_extra_syscalls.c's own header comment gives for why IT couldn't
 * just #include bash_syscalls.c either.
 *
 * fpathconf() and getpgrp() are genuinely new: no earlier tier in this
 * project ever needed them (confirmed by grep across
 * tools/userland_staging (*.c) before adding). Both are plain
 * non-NO_SYSCALL_STUB entries in syscalls.master, so a direct raw
 * syscall is the same real shape Apple's own Perl generator would have
 * produced.
 *
 * read()/select(): the public (non-underscore) names real ncurses calls
 * (lib_getch.c's fifo_push, tty_update.c/lib_twait.c's poll-for-input
 * path). The PRIVATE underscore-prefixed raw syscalls already exist and
 * are already linked in this build (libc_locale_syscalls.c's _read(),
 * part of libc_build/libc_locale.a; libc_gen_syscalls.c's _select(),
 * part of libc_build/libc_gen.a) -- same "public name is just a thin
 * forwarder over an already-real private syscall" pattern
 * simple_cmds_read_compat.c already established for read(), reused
 * verbatim in shape here for select() too.
 */
#include <signal.h>

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

/* #192 -- int sys_fpathconf(int fd, int name); public name is plain
 * fpathconf(2), same "raw syscall number, renamed at the libc wrapper
 * layer" shape as several other entries here. */
long fpathconf(int fd, int name) {
    return syscall3(192, fd, name, 0);
}

/* #81 -- int getpgrp(void) */
int getpgrp(void) {
    return (int)syscall3(81, 0, 0, 0);
}

/* Public read()/select() forwarders -- see header comment above. */
extern ssize_t _read(int fd, void *buf, size_t n);
ssize_t read(int fd, void *buf, size_t n) {
    return _read(fd, buf, n);
}

extern int _select(int nfds, void *readfds, void *writefds, void *exceptfds, void *timeout);
int select(int nfds, void *readfds, void *writefds, void *exceptfds, void *timeout) {
    return _select(nfds, readfds, writefds, exceptfds, timeout);
}

/* ---- verbatim reuse of bash_syscalls.c's already-verified sigaction
 * chain (that file's lines ~755-802, real Apple wrapper chain
 * sigaction() [Libc/sys/sigaction.c, compiled separately by
 * build_ncurses.sh] -> __platform_sigaction() [libplatform, compiled
 * separately] -> __sigaction() [this raw syscall] + real _sigtramp
 * signal-delivery trampoline). ---- */
extern int __sigaction(int sig, void *nsa, void *osa);
int __sigaction(int sig, void *nsa, void *osa) {
    return (int)syscall3(46, (long)sig, (long)nsa, (long)osa);
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

/* ---- verbatim reuse of cron_extra_syscalls.c's already-verified
 * public sigprocmask() (that file's #48 block) -- not pre-archived in
 * any curated libc_build archive (confirmed by nm), same as that file found. ---- */
int sigprocmask(int how, const sigset_t *set, sigset_t *oset) {
    return (int)syscall3(48, (long)how, (long)set, (long)oset);
}

/* ---- Additional real gaps found by actually compiling the full 164-file
 * real Apple Sources list and cross-checking `nm -u` on the resulting
 * archive against every already-built libc_build/*.a (same methodology
 * this file's own header comment describes -- not guessed). Every body
 * below is a VERBATIM reuse of an already-verified real implementation
 * from bash_syscalls.c or cron_extra_syscalls.c (both already
 * QEMU-boot-verified for these exact bodies), not reinvented -- cited
 * per block. Not #included directly for the same reason given at the
 * top of this file: both source files also define other symbols this
 * build already gets from elsewhere. ---- */

/* #9 -- int link(user_addr_t path, user_addr_t link) -- write_entry.c's
 * real link_tinfo()/_nc_write_entry() hard-link fallback when copying a
 * compiled terminfo entry to an alias name. Genuinely new: no earlier
 * tier in this project ever needed link(2) (confirmed by grep across
 * tools/userland_staging/*.c before adding). Syscall number cross-checked
 * against third_party/xnu-7195/bsd/kern/syscalls.master, same as every
 * other raw syscall in this file. */
int link(const char *path, const char *link_path) {
    return (int)syscall3(9, (long)path, (long)link_path, 0);
}

/* #12/#136 -- verbatim reuse of cron_extra_syscalls.c's already-verified
 * chdir()/mkdir() bodies (that file's own #12/#136 blocks) -- tic/infocmp-
 * adjacent terminfo-tree-writing paths (db_iterator.c/write_entry.c) walk
 * and create directories under the compiled terminfo tree the same way
 * cron's crontab spool handling does. */
int chdir(const char *path) {
    return (int)syscall3(12, (long)path, 0, 0);
}
int mkdir(const char *path, int mode) {
    return (int)syscall3(136, (long)path, (long)mode, 0);
}

/* #33 -- verbatim reuse of bash_syscalls.c's already-verified access()
 * body (that file's own #33 block) -- db_iterator.c's real terminfo-tree
 * search walks each candidate directory with access(path, R_OK) before
 * trying to open it. */
int access(const char *path, int flags) {
    return (int)syscall3(33, (long)path, (long)flags, 0);
}

/* #37 -- verbatim reuse of bash_syscalls.c's already-verified kill()
 * body (that file's own #37 block, including its real 3-arg BSD kill(2)
 * posix=1 convention -- see that file's own comment for why) --
 * lib_mouse.c's own signal-based mouse-event delivery path (SIGIO/xterm
 * mouse tracking) and lib_tstp.c's SIGTSTP re-raise-self-after-cleanup
 * pattern both call plain kill(getpid(), sig)-shaped code. */
int kill(int pid, int sig) {
    return (int)syscall3(37, (long)pid, (long)sig, 1);
}

/* #116 -- verbatim reuse of bash_syscalls.c's already-verified
 * gettimeofday() body (that file's own #116 block, including its real
 * "no commpage fast path wired on this target" reasoning for why a plain
 * raw syscall is the honest fallback) -- lib_twait.c's own input-timeout
 * pacing computes elapsed wall-clock time via gettimeofday() around its
 * select()/poll() wait loop. */
int gettimeofday(void *tp, void *tzp) {
    return (int)syscall3(116, (long)tp, (long)tzp, 0);
}

/* Verbatim reuse of bash_syscalls.c's already-verified portable FreeBSD-
 * shape strstr() (that file's own header comment: this project's
 * Libc-1439.40.11 checkout has no strstr.c at all under any architecture,
 * confirmed by find -- same real gap class build_libc_string.sh already
 * documented for memcpy/memset/strlen). lib_termcap.c's tgetent()
 * compatibility shim searches a termcap-style capability string for a
 * substring match. */
extern unsigned long strlen(const char *);
char *strstr(const char *big, const char *little) {
    unsigned long blen = strlen(big), llen = strlen(little);
    unsigned long i;
    if (llen == 0) return (char *)big;
    if (llen > blen) return (void *)0;
    for (i = 0; i + llen <= blen; i++) {
        unsigned long j;
        for (j = 0; j < llen && big[i + j] == little[j]; j++) {}
        if (j == llen) return (char *)&big[i];
    }
    return (void *)0;
}

/* Verbatim reuse of bash_syscalls.c's already-verified
 * __darwin_check_fd_set_overflow() (that file's own header comment: real
 * Apple hardening helper the SDK's <sys/_types/_fd_def.h> FD_SET/
 * FD_ISSET/FD_CLR macros call, which must genuinely resolve on a static
 * target with no weak-symbol libSystem fallback). lib_twait.c's real
 * input-wait path builds an fd_set for select() over stdin plus any
 * registered extra fds. */
#define NCURSES_FD_SETSIZE 1024
int __darwin_check_fd_set_overflow(int fd, const void *fd_set_ptr, int unlimited) {
    (void)fd_set_ptr;
    if (unlimited) {
        return 1;
    }
    return fd >= 0 && fd < NCURSES_FD_SETSIZE;
}

/* Plain (non-$UNIX2003) sleep(): lib_print.c references this exact
 * unversioned symbol directly (confirmed via nm on the compiled .o).
 * Every sibling tier's smoketest instead makes a linker alias from
 * libmalloc.a's real libmalloc_sleep_shim.c
 * (_mal_semwait_sleep -> symbol __mal_semwait_sleep) to
 * '_sleep$UNIX2003'; a SECOND alias from that same base symbol to the
 * plain, unversioned '_sleep' genuinely crashes ld64 itself (a real
 * "Assertion failed: (target != NULL), function generateLinkEditInfo"
 * internal linker bug, reproduced directly while wiring up this tier's
 * smoketest, not guessed).
 *
 * REAL BUG FOUND+FIXED (2026-09-15): this used to be a one-line forwarder
 * straight to _mal_semwait_sleep() -- but that function's own header
 * comment (libmalloc_sleep_shim.c) is explicit that it was written for
 * ONE rare, single-shot malloc debug pause that "doesn't need signal-
 * interrupt correctness", and deliberately skips real Apple/FreeBSD
 * sleep.c's EINTR-retry loop on that basis. This project's own real
 * PureDarwin launchd.c reuses plain sleep() for something that basis
 * does NOT hold for: update_thread()'s `while (launchd_sync_frequency)
 * { sync(); sleep(launchd_sync_frequency); }` -- and launchd, as real
 * PID 1, receives real SIGCHLD continuously (every process anywhere in
 * the system that exits gets reparented to PID 1 and reaped). Each
 * SIGCHLD interrupts the underlying __semwait_signal(2) with EINTR, the
 * old no-retry forwarder returned immediately instead of actually
 * waiting, and the loop went straight back into a fresh, real, expensive
 * sync() -> hfs_sync() -> vnode_iterate() pass -- confirmed via a real
 * QEMU monitor register-probe session (PC parked in
 * _lck_mtx_lock/_hw_lock_lock, called from vnode_iterate, called from
 * hfs_sync) plus a real temporary kernel diagnostic print showing
 * hfs_sync() entered over 23,000 times in a few seconds at an otherwise-
 * idle login prompt, host CPU pinned near 100%. Real fix: track real
 * elapsed time with mach_absolute_time() (already a real, linked
 * dependency of this project's own launchd runtime.o/CoreFoundation
 * CFDate.o, so no new gap) and retry __semwait_signal with the real
 * remaining duration on EINTR, same substance as real FreeBSD sleep.c's
 * own retry loop -- just without pulling in that whole implementation,
 * matching this file's own "thin forwarder over a real primitive" style.
 * _mal_semwait_sleep()/libmalloc_sleep_shim.c itself is UNTOUCHED: its
 * one real caller (malloc_report()'s debug-only pause) genuinely doesn't
 * need this, and it's a real, narrowly-scoped shim, not a general-
 * purpose one -- this file no longer forwards to it for that reason. */
/* FIXED 2026-09-15 (DAR-309): this loop used to pass 0 as __semwait_signal's
 * cond_sem. That is not a valid semaphore port name, so the real kernel
 * failed the call INSTANTLY instead of waiting -- port_name_to_semaphore()
 * (osfmk/kern/ipc_sync.c:63) returns KERN_INVALID_NAME for !MACH_PORT_VALID(0)
 * without ever reaching semaphore_wait_internal(). The elapsed-time retry loop
 * below then span on that instantly-failing call for the WHOLE sleep duration,
 * burning ~100% CPU while still returning at the correct wall-clock time --
 * which is exactly why an iteration-count diagnostic on launchd's own
 * update_thread() looked perfectly healthy (correct ~30s cadence) while the
 * host sat at ~97% CPU on both QEMU and real Pi 3 hardware.
 *
 * Real Apple passes a REAL semaphore here (third_party/Libc/gen/nanosleep.c:104,
 * SEMWAIT_SIGNAL(clock_sem, MACH_PORT_NULL, 1, 1, sec, nsec)); mach_ipc_clock_init()
 * creates one the same way real Apple's own _init_clock_port() does. The
 * elapsed-time retry loop itself is KEPT deliberately -- it is the real,
 * still-needed part of the earlier fix: launchd is real PID 1 and takes
 * continuous real SIGCHLD, each of which interrupts the wait with EINTR, and
 * without re-waiting the remaining duration update_thread() would go straight
 * back into a fresh, expensive sync() -> hfs_sync() pass (the original
 * 23,000-hfs_sync storm). Now each retry genuinely BLOCKS for the remaining
 * time instead of spinning on it. */
#include <mach/mach_time.h>

extern int __semwait_signal(int cond_sem, int mutex_sem, int timeout,
    int relative, long long tv_sec, int tv_nsec);
extern void mach_ipc_clock_init(void);
extern mach_port_t g_mach_ipc_clock_sem;

unsigned int sleep(unsigned int seconds) {
    if (seconds == 0) {
        return 0;
    }

    mach_timebase_info_data_t tb = {0, 0};
    mach_timebase_info(&tb);
    if (tb.numer == 0 || tb.denom == 0) {
        tb.numer = 1;
        tb.denom = 1;
    }

    mach_ipc_clock_init();

    unsigned long long target_ns = (unsigned long long)seconds * 1000000000ULL;
    uint64_t start = mach_absolute_time();

    for (;;) {
        uint64_t elapsed_ticks = mach_absolute_time() - start;
        unsigned long long elapsed_ns =
            (unsigned long long)elapsed_ticks * tb.numer / tb.denom;
        if (elapsed_ns >= target_ns) {
            break;
        }
        unsigned long long remaining_ns = target_ns - elapsed_ns;
        __semwait_signal(g_mach_ipc_clock_sem, 0, 1, 1,
            (long long)(remaining_ns / 1000000000ULL),
            (int)(remaining_ns % 1000000000ULL));
    }
    return 0;
}

/* pthread_testcancel(): libc_gen.a's real termios.o (tcdrain()) calls this
 * unconditionally. This project has no pthread cancellation
 * infrastructure at all (same real scope decision
 * simple_cmds_nanosleep_pub_compat.c's own header comment already made
 * for __semwait_signal vs __semwait_signal_nocancel -- no cancellation
 * request can ever actually be pending), so a real no-op is the correct,
 * non-fabricated body -- verbatim reuse of bash_syscalls.c's own
 * identical `void pthread_testcancel(void) {}` (that file's own comment:
 * "it's simplest, no common intermediate archive for this one symbol").
 * Deliberately NOT wired up via `-Wl,-alias` to
 * '_pthread_testcancel$UNIX2003' the way build_libc_regex.sh's/
 * build_libiconv.sh's own smoketests do: with an undefined base symbol,
 * that alias combined with this tier's real (bidirectional, reverse-
 * direction) pthread_rwlock_{wrlock,rdlock,unlock}$UNIX2003 aliases
 * genuinely crashes ld64 itself (the same
 * "Assertion failed: (target != NULL)" internal linker bug noted above
 * for sleep(), reproduced directly by bisecting this tier's alias list
 * one entry at a time, not guessed) -- a real function body sidesteps
 * the whole fragile alias/undefined-base interaction. */
void pthread_testcancel(void) {
}
