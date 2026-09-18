/* The two real symbols vim.macho needs that neither an already-built
 * dylib exports nor Apple's own create-syscalls.pl generator can emit
 * (DAR-185). Same real carry-flag raw-syscall ABI / cerror_nocancel()
 * convention, and the same "cross-check syscalls.master, don't guess"
 * method, as this project's removefile_syscalls.c / cp_mv_syscalls.c /
 * crontab_extra_syscalls.c already document.
 *
 * Deliberately narrow: vim links none of those files wholesale because
 * each also defines fork/unlink/read/umask/... that real
 * libsystem_c.dylib now exports for real (DAR-292), and a static
 * definition silently shadowing a dylib export is this project's
 * repeatedly-hit duplicate-static-copy bug class (see
 * docs/launchd-pid1-postmortem.md).
 */
#include <sys/types.h>
#include <signal.h>

extern long cerror_nocancel(int err);

static inline long vsyscall2(long num, long a0, long a1) {
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

/* bsd/kern/syscalls.master #128 -- `{ int rename(char *from, char *to)
 * NO_SYSCALL_STUB; }` (third_party/xnu-7195/bsd/kern/syscalls.master:201,
 * read directly), so no generated .s stub exists for it. Real callers
 * here: fileio.c's vim_rename() (every :w of an existing file, via the
 * backup-then-rename path). Implementing the real PUBLIC name straight
 * from the trap is exactly removefile_syscalls.c's own established
 * precedent for this syscall (that file's rename() is the same code);
 * vim links a two-function file instead of that whole one for the
 * shadowing reason in this file's header. */
int rename(const char *from, const char *to) {
    return (int)vsyscall2(128, (long)from, (long)to);
}

/* Real Apple Libc/gen/FreeBSD/signal.c's signal__() calls _sigaction()
 * when its `bind` argument is set and _sigaction_nobind() otherwise,
 * both under `#if defined(__DYNAMIC__)` -- which this Apple-targeted
 * compiler predefines, so BOTH are real undefined symbols of the
 * signal.o this build links (third_party/Libc/gen/FreeBSD/signal.c:49
 * and :64-74, read directly; confirmed by `nm` on the built signal.o
 * showing `U __sigaction` + `U __sigaction_nobind`).
 *
 * Unlike cron (cron_extra_syscalls.c, which had no sigaction of its own
 * and had to bring its own raw trap + __sigtramp trampoline), vim links
 * real libsystem_c.dylib, which exports a real, working public
 * `_sigaction` (confirmed: `nm -gU libc_build/system/libsystem_c.dylib`
 * -> `T _sigaction`) that vim's own objects already call directly. So
 * both names forward to that one real implementation -- the same
 * forwarder cp_mv_syscalls.c:127 already uses for _sigaction. Apple's
 * own bind/nobind split is about which thread's exception ports the
 * handler registration touches, not about what handler gets installed;
 * vim is single-threaded (no pthread_create call site anywhere in its
 * BASIC_SRC), so no call here can observe the difference. */
int _sigaction(int sig, const struct sigaction *nsv, struct sigaction *osv) {
    return sigaction(sig, nsv, osv);
}
int _sigaction_nobind(int sig, const struct sigaction *nsv, struct sigaction *osv) {
    return sigaction(sig, nsv, osv);
}
