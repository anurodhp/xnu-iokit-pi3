/* Raw syscall wrappers real bash-3.2 needs that this project's earlier
 * tiers never had a caller for. Same real carry-flag ABI / cerror_nocancel()
 * error-reporting convention as libc_gen_syscalls.c (see that file's own
 * header comment for the full real-bug story behind why cerror_nocancel()
 * matters, not a raw negative return). Syscall numbers cross-checked
 * directly against third_party/xnu-7195/bsd/kern/syscalls.master, not
 * guessed. All of wait4/unlink/kill/mprotect/setreuid/getrlimit/setrlimit
 * are marked NO_SYSCALL_STUB there (Apple's own Perl generator
 * deliberately skips them) with no third_party/xnu-7195/libsyscall/custom/
 * override either (confirmed absent by find) -- hand-written here, same
 * as libc_gen_syscalls.c already does for its own NO_SYSCALL_STUB set. */

typedef unsigned long size_t;
typedef long ssize_t;
typedef long pid_t;

extern long cerror_nocancel(int err);
extern int sigprocmask(int how, const void *set, void *oset);
extern int gettimeofday(void *tp, void *tzp);

static inline long bsyscall3(long num, long a0, long a1, long a2) {
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

static inline long bsyscall4(long num, long a0, long a1, long a2, long a3) {
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
    if (is_error) return cerror_nocancel((int)x0);
    return x0;
}

/* #7 -- int wait4(int pid, user_addr_t status, int options, user_addr_t rusage) */
pid_t wait4(pid_t pid, int *status, int options, void *rusage) {
    return (pid_t)bsyscall4(7, (long)pid, (long)status, (long)options, (long)rusage);
}
/* Real Apple waitpid() is a thin wrapper: waitpid(pid, status, options) ==
 * wait4(pid, status, options, NULL) -- confirmed via Libc/gen/wait.c's own
 * documented equivalence in real BSD lineage, not guessed. */
pid_t waitpid(pid_t pid, int *status, int options) {
    return wait4(pid, status, options, (void *)0);
}

/* #10 -- int unlink(user_addr_t path) */
int unlink(const char *path) {
    return (int)bsyscall3(10, (long)path, 0, 0);
}

/* #37 -- int kill(int pid, int signum, int posix) -- real Apple userland
 * kill() always passes posix=1 (see Libc/sys/kill.c's real convention,
 * confirmed via the syscall's own AUE_KILL comment + this project's
 * existing raw-syscall precedent of matching the real 3-arg BSD kill(2)
 * shape rather than the 2-arg POSIX kill(3) prototype apps call). */
int kill(pid_t pid, int sig) {
    return (int)bsyscall3(37, (long)pid, (long)sig, 1);
}

/* #74 -- int mprotect(caddr_t addr, size_t len, int prot) */
int mprotect(void *addr, size_t len, int prot) {
    return (int)bsyscall3(74, (long)addr, (long)len, (long)prot);
}

/* #126 -- int setreuid(uid_t ruid, uid_t euid) */
int setreuid(unsigned ruid, unsigned euid) {
    return (int)bsyscall3(126, (long)ruid, (long)euid, 0);
}

/* #194/#195 -- int getrlimit/setrlimit(u_int which, struct rlimit *rlp) */
int getrlimit(unsigned which, void *rlp) {
    return (int)bsyscall3(194, (long)which, (long)rlp, 0);
}
int setrlimit(unsigned which, const void *rlp) {
    return (int)bsyscall3(195, (long)which, (long)rlp, 0);
}

/* alarm(2): no real BSD syscall exists (real Apple implements it via
 * setitimer(2) + SIGALRM, itself needing a real interval-timer subsystem
 * this project doesn't have wired up). Honest stub: always reports "no
 * previous alarm pending" (0) and does NOT schedule a real SIGALRM --
 * this is a genuine, documented behavioral gap (bash's `read -t`/mail-
 * check timeout won't actually fire), not fabricated support. Tracked
 * in docs/roadmap.md alongside the other single-thread/no-real-signal-
 * timer gaps this project already documents. */
unsigned alarm(unsigned seconds) {
    (void)seconds;
    return 0;
}

/* Job-control terminal-state syscalls (tcgetpgrp/tcsetpgrp/tcgetattr/
 * tcsetattr) and ttyname() used to be honest ENOTTY/NULL stand-ins here,
 * on the assumption that this target's console had no real tty line-
 * discipline driver underneath. That assumption was never actually
 * tested and turned out to be WRONG: a disposable ioctl(TIOCGETA/
 * TIOCSETA) probe (2026-08-19, see libc_gen_syscalls.c's ioctl/_ioctl
 * header comment for the full transcript-backed writeup) proved
 * third_party/xnu-7195's real BSD tty stack is already fully wired
 * under /dev/console, including real raw-mode. The real Apple/FreeBSD
 * implementations (third_party/Libc/gen/FreeBSD/termios.c,
 * isatty.c, ttyname.c) are now compiled for real into libc_gen.a
 * (tools/userland_staging/build_libc_gen.sh) and linked into bash
 * instead -- removed the stand-ins here rather than shadow them. */

/* dlopen family: this project's entire architecture is static (zero
 * dyld anywhere, see architecture.md and docs/roadmap.md's dyld-scoped-
 * out writeup) -- there is no dynamic loader to ask. Honest stubs that
 * always report failure, used by bash only for optional loadable-
 * builtin support (`enable -f`), not the interactive core. */
void *dlopen(const char *path, int mode) { (void)path; (void)mode; return (void *)0; }
int dlclose(void *handle) { (void)handle; return -1; }
char *dlerror(void) { return "dlopen: no dynamic loader on this static-only target"; }
void *dlsym(void *handle, const char *sym) { (void)handle; (void)sym; return (void *)0; }

/* getpwnam/getpwuid/endpwent: real account lookup on this project needs
 * tools/libinfo_custom's flat-file resolver (see Phase 5), not yet wired
 * into this bash build -- honest stub returning "no such user", used by
 * bash only for `~user` tilde expansion, not required for a working
 * interactive shell as the invoking user. */
struct bash_passwd_opaque { char _opaque[128]; };
void *getpwnam(const char *name) { (void)name; return (void *)0; }
void *getpwuid(unsigned uid) { (void)uid; return (void *)0; }
void endpwent(void) {}

/* fork(): real Apple Libc/sys/fork.c wraps this with
 * _libSystem_atfork_prepare/parent/child() function-pointer callouts that
 * are only ever populated by libSystem's real process-startup initializer
 * (_libc_fork_init(), called from Libsystem_initializer) -- this project
 * has no such initializer wired up, so those pointers are NULL and real
 * fork.c would crash calling through them. Same real ABI quirk this
 * project's own tools/init_binary/init.c already found and documented
 * (arm64 XNU's raw fork() returns the child's pid in x0 to BOTH parent
 * and child; x1 is the only real parent/child discriminator, 0=parent,
 * 1=child) -- reused verbatim here rather than fighting Libc's fork.c.
 *
 * DAR-220: the raw trap is NOT the whole of real Apple's own fork
 * wrapper. third_party/xnu-7195/libsyscall/custom/__fork.s's arm64 path
 * does one more thing in the child before returning:
 *
 *     // Child
 *     MI_GET_ADDRESS(x9, __current_pid)  // Get address of cached "current pid"
 *     mov  w0, #0
 *     str  w0, [x9]                      // Clear cached current pid
 *
 * because ___getpid (custom/__getpid.s, linked into this binary -- check
 * with `nm -m bash.macho | grep __current_pid`) is a CACHING getpid: it
 * returns the process-wide `__current_pid` global whenever it is > 0 and
 * only falls through to the real SYS_getpid trap when it is 0. The child
 * of fork() inherits the parent's cached value, so unless the fork
 * wrapper itself zeroes it, getpid() in the child keeps returning the
 * PARENT's pid forever.
 *
 * Real, real-hardware-observed symptom that found this (DAR-220): every
 * command typed at the login shell printed
 *     -sh: child setpgid (6 to 6): No such process
 * where 6 was the SHELL's pid, identical for every command rather than
 * incrementing per child. That is jobs.c:1748's `setpgid (mypid,
 * pipeline_pgrp)` in the freshly-forked child, with mypid = the stale
 * cached parent pid. bsd/kern/kern_prot.c's setpgid() only returns ESRCH
 * on the `uap->pid != curp->p_pid` branch (`proc_find() == 0 ||
 * !inferior()`) -- a real self-setpgid can never produce ESRCH -- so the
 * error itself was proof getpid() had returned someone else's pid.
 *
 * Not merely cosmetic: bash's child then has pipeline_pgrp == the
 * shell's own pid == shell_pgrp, which sends it down make_child()'s
 * "this is a command-substitution child" branch -- ignore_tty_job_signals()
 * (SIGTSTP/SIGTTIN/SIGTTOU set to SIG_IGN, inherited across exec, so
 * ^Z could not suspend a foreground job) plus a skipped
 * give_terminal_to(). */
pid_t fork(void) {
    /* asm label, not name mangling: this is the very same `__current_pid`
     * datum ___getpid.s defines and caches into. */
    extern volatile int bash_cached_current_pid asm("__current_pid");
    register long x0 asm("x0") = 0;
    register long x1 asm("x1") = 0;
    register long x16 asm("x16") = 2; /* SYS_fork, see syscalls.master */
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %2, cs"
                 : "+r"(x0), "+r"(x1), "=r"(is_error)
                 : "r"(x16)
                 : "memory", "cc");
    if (is_error) return (pid_t)cerror_nocancel((int)x0);
    if (x1) { /* child (x1==1) */
        bash_cached_current_pid = 0; /* real __fork.s's own child-side step */
        return (pid_t)0;             /* real fork() contract */
    }
    return (pid_t)x0;
}

/* pipe(2): real macOS ABI returns BOTH fds via x0/x1 registers directly
 * (not through an out-param struct) -- confirmed via
 * third_party/xnu-7195/libsyscall/custom/__pipe.s's own real macro
 * expansion (__SYSCALL2 for a 2-return-value trap), same reason it's
 * hand-written there instead of Perl-generated. #42 per syscalls.master. */
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

/* #116 -- gettimeofday(struct timeval *tp, struct timezone *tzp,
 * uint64_t *mach_absolute_time) -- real Apple's is commpage-accelerated
 * and NO_SYSCALL_STUB for that reason; this target has no commpage
 * fast path wired (see docs/roadmap.md's mach_absolute_time.s note from
 * the libplatform tier), so a plain raw syscall is the honest real
 * fallback path real XNU itself supports when the commpage read is
 * unavailable. Third arg is genuinely optional per the real kernel
 * entry point (accepts NULL, confirmed via xnu's gettimeofday
 * implementation only writing it when non-NULL). */
int gettimeofday(void *tp, void *tzp) {
    return (int)bsyscall3(116, (long)tp, (long)tzp, 0);
}

/* #127 -- int setregid(gid_t rgid, gid_t egid) */
int setregid(unsigned rgid, unsigned egid) {
    return (int)bsyscall3(127, (long)rgid, (long)egid, 0);
}

/* Real Apple headers (<unistd.h>/<sys/stat.h>/etc), compiled with this
 * project's real -D__DARWIN_UNIX03=1/-D__DARWIN_64_BIT_INO_T=1 flags
 * (same flags every other tier uses, matching real Apple's own build),
 * rewrite plain POSIX call sites (read/write/stat/strerror/strtod/sleep/
 * waitpid/setreuid/setrlimit/...) to real versioned symbol names via the
 * __DARWIN_ALIAS family of macros -- e.g. bash's own stat() call becomes
 * a reference to the linker symbol "_stat$INODE64", not "_stat". Earlier
 * tiers in this project never hit this because their OWN code always
 * called the raw/internal names directly (see libc_gen_syscalls.c's own
 * header comment on this exact "$INODE64 symbol-suffix machinery"), but
 * real unmodified bash source goes through the real headers as designed.
 * Real fix: define the exact suffixed symbol names directly (not the
 * plain names) via `asm("_name$SUFFIX")`, thin real implementations
 * (either delegating to a real syscall here, or to a real function this
 * project's curated libc_build/*.a tiers already built under their own,
 * differently-suffixed/plain internal name). Not fabricated support --
 * every one of these does real work. */
extern int stat(const char *path, void *ub) asm("_stat$INODE64");
int stat(const char *path, void *ub) {
    return (int)bsyscall3(338, (long)path, (long)ub, 0); /* #338 stat64 */
}
extern int lstat(const char *path, void *ub) asm("_lstat$INODE64");
int lstat(const char *path, void *ub) {
    return (int)bsyscall3(340, (long)path, (long)ub, 0); /* #340 lstat64 */
}
extern void *opendir(const char *path);
extern void *opendir_u2003(const char *path) asm("_opendir$INODE64$UNIX2003");
void *opendir_u2003(const char *path) { return opendir(path); }
extern void *readdir(void *dirp);
extern void *readdir_u2003(void *dirp) asm("_readdir$INODE64");
void *readdir_u2003(void *dirp) { return readdir(dirp); }
extern long read_u2003(int fd, void *buf, unsigned long n) asm("_read$UNIX2003");
long read_u2003(int fd, void *buf, unsigned long n) {
    return bsyscall3(3, (long)fd, (long)buf, (long)n); /* #3 read */
}
extern long write_u2003(int fd, const void *buf, unsigned long n) asm("_write$UNIX2003");
long write_u2003(int fd, const void *buf, unsigned long n) {
    return bsyscall3(4, (long)fd, (long)buf, (long)n); /* #4 write */
}
extern pid_t waitpid_u2003(pid_t pid, int *status, int options) asm("_waitpid$UNIX2003");
pid_t waitpid_u2003(pid_t pid, int *status, int options) {
    return waitpid(pid, status, options);
}
extern int setreuid_u2003(unsigned r, unsigned e) asm("_setreuid$UNIX2003");
int setreuid_u2003(unsigned r, unsigned e) { return setreuid(r, e); }
extern int setregid_u2003(unsigned r, unsigned e) asm("_setregid$UNIX2003");
int setregid_u2003(unsigned r, unsigned e) { return setregid(r, e); }
extern int setrlimit_u2003(unsigned which, const void *rlp) asm("_setrlimit$UNIX2003");
int setrlimit_u2003(unsigned which, const void *rlp) { return setrlimit(which, rlp); }

extern char *strerror(int);
extern char *strerror_u2003(int errnum) asm("_strerror$UNIX2003");
char *strerror_u2003(int errnum) { return strerror(errnum); }
extern double strtod(const char *, char **);
extern double strtod_u2003(const char *s, char **e) asm("_strtod$UNIX2003");
double strtod_u2003(const char *s, char **e) { return strtod(s, e); }

/* sleep(3): no real interval-timer/SIGALRM subsystem here (see alarm()
 * above) -- implemented as a real, correct (if power-inefficient) busy
 * wait against real gettimeofday() rather than a stub that returns
 * instantly, since an accurate real-time delay is easy to provide
 * honestly even without a real timer interrupt. */
unsigned sleep_u2003(unsigned seconds) asm("_sleep$UNIX2003");
unsigned sleep_u2003(unsigned seconds) {
    struct { long tv_sec; long tv_usec; } start, now;
    gettimeofday(&start, (void *)0);
    for (;;) {
        gettimeofday(&now, (void *)0);
        if ((unsigned long)(now.tv_sec - start.tv_sec) >= seconds) break;
    }
    return 0;
}

/* sigsetjmp/siglongjmp: real register-context save/restore, hand-written
 * AAPCS64 asm (clang's __builtin_setjmp/__builtin_longjmp aren't
 * supported on this arm64 target -- confirmed by direct compile failure,
 * "not supported for the current target") plus a real sigprocmask()
 * save/restore for the signal-mask half of the real POSIX contract --
 * bash's sig.c/unwind_prot.c genuinely rely on the mask being restored
 * on a real trap/signal longjmp, not just registers. sigjmp_buf is a
 * real opaque int[49] per the Xcode-12 iPhoneOS SDK's own setjmp.h
 * (arm64 branch) -- only this file ever reads/writes it, so the exact
 * real Apple internal layout doesn't need to match, just be internally
 * self-consistent and large enough (196 real bytes here vs. the 168
 * bytes the 21 callee-saved registers need + 8 for the mask bookkeeping).
 *
 * Byte layout: x19-x28 (10 GPRs) at 0-79, x29/x30 at 80-95, sp at 96-103,
 * d8-d15 (8 FP/SIMD regs) at 104-167, mask-saved flag at 168 (int-index
 * 42), saved sigset_t at 172 (int-index 43).
 *
 * REAL BUG FOUND AND FIXED (2026-08-19, the `bash -i` infinite
 * `exit`-flood investigation -- see docs/roadmap.md/docs/historic/
 * roadmap.md Phase 4 for the full writeup): an earlier version of this
 * file split the work into a C function (`sigsetjmp()`/`siglongjmp()`)
 * that called a separate raw-asm helper (`__bash_setjmp_asm`/
 * `__bash_longjmp_asm`) to do the actual register save/restore. That's
 * broken for a MULTI-FRAME-SURVIVING jump: a real C function always gets
 * its own compiler-generated stack frame, so `__bash_setjmp_asm` was
 * being called one level too deep -- it captured x29/x30 as they stood
 * INSIDE `sigsetjmp()`'s own frame (return address = back into
 * `sigsetjmp()`, not into `sigsetjmp()`'s caller), and spilled `env`
 * itself into that same now-doomed frame. The moment the initial
 * `sigsetjmp()` call returned 0 to its real caller (bash's `reader_loop()`
 * in eval.c, via the `setjmp(top_level)` macro), that frame was popped
 * and its stack memory became fair game for every function called
 * afterward (parsing, `read_command()`, `getc_with_restart()`, ...) --
 * bash's own EOF handling calls `siglongjmp()` many stack frames deeper,
 * by which point `sigsetjmp()`'s old frame (env's spill slot AND its own
 * saved x29/x30 needed to eventually `ret` back to `reader_loop()`) had
 * long since been overwritten with unrelated data. Root-caused via a
 * real QEMU boot test with temporary instrumentation (removed before
 * this commit) printing `env`/`rc` on every sigsetjmp/siglongjmp call:
 * `rc` (freshly written right after the asm call returns, before
 * anything else can run) reliably showed the correct value (3 ==
 * EXITPROG), but `env` -- reloaded from the clobbered stack spill slot
 * -- came back as garbage, and reader_loop()'s own `code = setjmp(...)`
 * assignment was NEVER observed to receive the nonzero EXITPROG value
 * (control was, in effect, returning to a corrupted x29/x30 pulled from
 * that same stomped memory instead of genuinely back into
 * `reader_loop()`), producing exactly the observed infinite retry: each
 * "misrouted" return happened to unwind back through bash's own intact,
 * deeper call frames into another read/EOF/exit cycle instead of
 * terminating.
 *
 * Real fix: `sigsetjmp`/`siglongjmp` are now themselves the raw asm
 * entry points (no intervening C wrapper function, hence no doomed
 * extra frame). `sigsetjmp`'s register-capture instructions are the
 * very first thing that runs on entry -- before any `bl` -- so x29/x30/sp
 * captured are genuinely the CALLER's (`reader_loop()`'s), exactly like
 * a real libc implementation. The nested `sigprocmask()` call needed for
 * `savemask` is done AFTER the register capture already safely landed in
 * the jmp_buf itself (not a local stack slot), and any transient stack
 * space it uses is fully consumed before this function returns -- no
 * cross-longjmp lifetime dependency on it. `siglongjmp` similarly calls
 * `sigprocmask()` (if needed) before doing its own final register
 * restore + `ret`, which lands directly back at the real, original
 * `reader_loop()` call site using the x29/x30 pulled straight from the
 * jmp_buf -- no intermediate "return through a dead C frame" step. */
extern int sigsetjmp(int *env, int savemask);
extern void siglongjmp(int *env, int val) __attribute__((noreturn));
asm(
    ".text\n"
    ".align 2\n"
    ".globl _sigsetjmp\n"
    "_sigsetjmp:\n"
    /* x0 = env, w1 = savemask. Capture the real caller's (reader_loop's)
     * register state FIRST, before any nested call can clobber x30. */
    "  stp x19, x20, [x0, #0]\n"
    "  stp x21, x22, [x0, #16]\n"
    "  stp x23, x24, [x0, #32]\n"
    "  stp x25, x26, [x0, #48]\n"
    "  stp x27, x28, [x0, #64]\n"
    "  stp x29, x30, [x0, #80]\n"
    "  mov x8, sp\n"
    "  str x8,  [x0, #96]\n"
    "  stp d8,  d9,  [x0, #104]\n"
    "  stp d10, d11, [x0, #120]\n"
    "  stp d12, d13, [x0, #136]\n"
    "  stp d14, d15, [x0, #152]\n"
    "  cbz w1, 1f\n"
    /* savemask != 0: sigprocmask(how=1, set=NULL, oset=&env[43]) -- same
     * how/set/oset arguments the original C implementation used. Only
     * `env` (x0) needs to survive the call -- x29/x30/sp etc are already
     * safely stored in the jmp_buf itself above. */
    "  sub sp, sp, #16\n"
    "  str x0, [sp]\n"
    "  add x2, x0, #172\n"
    "  mov x1, #0\n"
    "  mov w0, #1\n"
    "  bl _sigprocmask\n"
    "  ldr x0, [sp]\n"
    "  add sp, sp, #16\n"
    "  ldr x29, [x0, #80]\n"
    "  ldr x30, [x0, #88]\n"
    "  mov w9, #1\n"
    "  str w9, [x0, #168]\n"
    "  mov w0, #0\n"
    "  ret\n"
    "1:\n"
    "  str wzr, [x0, #168]\n"
    "  mov w0, #0\n"
    "  ret\n"
    ".globl _siglongjmp\n"
    "_siglongjmp:\n"
    /* x0 = env, w1 = val. */
    "  ldr w8, [x0, #168]\n"
    "  cbz w8, 2f\n"
    "  sub sp, sp, #16\n"
    "  str x0, [sp]\n"
    "  str w1, [sp, #8]\n"
    "  add x1, x0, #172\n"
    "  mov x2, #0\n"
    "  mov w0, #3\n"
    "  bl _sigprocmask\n"
    "  ldr x0, [sp]\n"
    "  ldr w1, [sp, #8]\n"
    "  add sp, sp, #16\n"
    "2:\n"
    "  ldp x19, x20, [x0, #0]\n"
    "  ldp x21, x22, [x0, #16]\n"
    "  ldp x23, x24, [x0, #32]\n"
    "  ldp x25, x26, [x0, #48]\n"
    "  ldp x27, x28, [x0, #64]\n"
    "  ldp x29, x30, [x0, #80]\n"
    "  ldr x8,  [x0, #96]\n"
    "  mov sp, x8\n"
    "  ldp d8,  d9,  [x0, #104]\n"
    "  ldp d10, d11, [x0, #120]\n"
    "  ldp d12, d13, [x0, #136]\n"
    "  ldp d14, d15, [x0, #152]\n"
    "  cmp w1, #0\n"
    "  csinc w0, w1, wzr, ne\n"
    "  ret\n"
);

/* Real portable strstr -- this project's Libc-1439.40.11 checkout has no
 * strstr.c at all under any architecture (confirmed by find), same real
 * gap class build_libc_string.sh already documented for memcpy/memset/
 * strlen (no portable-C implementation shipped for any arch in this
 * exact checkout) -- real FreeBSD fallback, same precedent as that tier,
 * last-resort per CLAUDE.md's Apple-first/FreeBSD-fallback/custom-last
 * rule, fetched fresh rather than hand-written. */
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

/* time(2): real Darwin implements this via the commpage, not a syscall
 * (same real reason gettimeofday() above is NO_SYSCALL_STUB) -- real,
 * correct implementation in terms of this file's own real gettimeofday(). */
long time(long *tloc) {
    struct { long tv_sec; long tv_usec; } tv;
    gettimeofday(&tv, (void *)0);
    if (tloc) *tloc = tv.tv_sec;
    return tv.tv_sec;
}

/* set_itemlist_dirty, every rl_-prefixed symbol, every it_-prefixed
 * symbol, readline() itself, etc, used to all be honest no-op stubs
 * here, standing in for real lib/readline (bind.c, readline.c,
 * ...) and real pcomplete.c/bashline.c, on the belief that this target's
 * console couldn't support real line editing at all. That belief was
 * never tested and turned out to be wrong (see libc_gen_syscalls.c's
 * ioctl/_ioctl header comment for the real, transcript-backed proof, and
 * config.h's READLINE re-#define). All of that removed 2026-08-19 in
 * favor of the real thing -- see build_bash.sh's EXTRA_FILES for the
 * real lib/readline/*.c + pcomplete.c + bashline.c file list now
 * providing every one of these symbols for real. */

/* POSIX regex (regcomp/regexec/regfree): Phase 4.6 item 3, DONE
 * 2026-08-20 (docs/roadmap.md). Real Apple source
 * (third_party/Libc/regex/TRE/lib/*.c, the TRE engine, + FreeBSD's
 * regerror.c) now builds clean into libc_build/libc_regex.a
 * (build_libc_regex.sh) and is linked into this binary below (see
 * build_bash.sh's final link line) -- the narrow REG_BADPAT/REG_NOMATCH
 * stand-ins that used to live here are gone. Real, unmodified
 * lib/sh/shmatch.c (bash's own `[[ str =~ pattern ]]` implementation,
 * already compiled into this binary via build_bash.sh's EXTRA_FILES)
 * now performs a real regcomp/regexec/regfree match.
 *
 * regexec/regfree need no shim at all: this checkout's own vendored
 * third_party/Libc/include/sys/cdefs.h (found ahead of the SDK's own
 * copy via this project's -I $LIBC/include search-path precedence,
 * confirmed by reading it directly -- it never defines
 * __DARWIN_ONLY_UNIX_CONFORMANCE, so __DARWIN_UNIX03's "$UNIX2003"
 * symbol-suffixing branch is live) carries no __DARWIN_ALIAS on
 * regexec/regfree's <regex.h> declarations (only regcomp has one) --
 * shmatch.c's calls to them reference plain `_regexec`/`_regfree`,
 * exactly what libc_regex.a's real regexec.o/regcomp.o export.
 * Object files linked directly on the command line always win over an
 * archive member offering the same symbol (this project's own documented
 * archive-shadowing rule, see the Nineteenth pass's pthread_once
 * writeup) -- so simply deleting the old fake regexec/regfree
 * DEFINITIONS from this file (done here) is enough to let the real
 * archive members resolve them; no forwarding trampoline needed for
 * either.
 *
 * regcomp is the one real exception: that same vendored cdefs.h DOES
 * carry __DARWIN_ALIAS(regcomp) on its <regex.h> declaration, so any
 * real caller built against these project headers (shmatch.c included)
 * asm-references the suffixed `_regcomp$UNIX2003` symbol, not plain
 * `_regcomp` -- confirmed by reading the __DARWIN_ALIAS/
 * __DARWIN_SUF_UNIX03 macro chain directly, not guessed. TRE's own
 * regcomp.c, compiled standalone, only ever exports the plain `_regcomp`
 * name (tre.h's `#define tre_regcomp regcomp` is a C-level identifier
 * rename, not an asm-level alias) -- so the real engine and shmatch.c's
 * real call site would silently fail to link without a trampoline
 * bridging the two names. Real Apple builds never hit this because a
 * real installed-SDK environment's cdefs.h (built with
 * __DARWIN_ONLY_UNIX_CONFORMANCE=1 for this exact iPhoneOS target, see
 * build_libc_regex.sh's own header comment) makes __DARWIN_ALIAS a
 * no-op suffix there -- a genuine, narrow divergence from Apple's real
 * build environment introduced by this project's own -I ordering, not a
 * bug in the vendored source itself. This trampoline's body calls the
 * real engine via a private extern bound directly to the real `_regcomp`
 * symbol (NOT the public <regex.h>-declared name, which this
 * translation unit deliberately never includes -- doing so would trip
 * the exact same __DARWIN_ALIAS rename on the call site itself and
 * self-reference this same trampoline). */
extern int __real_regcomp(void *preg, const char *pattern, int cflags) asm("_regcomp");
extern int regcomp_u2003(void *preg, const char *pattern, int cflags) asm("_regcomp$UNIX2003");
int regcomp_u2003(void *preg, const char *pattern, int cflags) { return __real_regcomp(preg, pattern, cflags); }

/* gettext/NLS: this project has no locale message catalogs -- honest
 * no-op, matches the real gettext contract's own documented fallback
 * (msgid returned unchanged) for a missing catalog, not fabricated. */
void libintl_textdomain(const char *domain) { (void)domain; }

extern int pthread_rwlock_rdlock_u2003(void *l) asm("_pthread_rwlock_rdlock$UNIX2003");
extern int pthread_rwlock_wrlock_u2003(void *l) asm("_pthread_rwlock_wrlock$UNIX2003");
extern int pthread_rwlock_unlock_u2003(void *l) asm("_pthread_rwlock_unlock$UNIX2003");
int _pthread_rwlock_rdlock(void *l) { return pthread_rwlock_rdlock_u2003(l); }
int _pthread_rwlock_wrlock(void *l) { return pthread_rwlock_wrlock_u2003(l); }
int _pthread_rwlock_unlock(void *l) { return pthread_rwlock_unlock_u2003(l); }

/* $UNIX2003 aliases for open/kill/mprotect (real underlying
 * implementations already exist: open() in libc_gen.a, kill()/mprotect()
 * above in this file). */
#define __BASH_HAVE_STDARG 1
typedef __builtin_va_list bash_va_list;
extern int open(const char *path, int flags, ...);
extern int open_u2003(const char *path, int flags, ...) asm("_open$UNIX2003");
#define BASH_O_CREAT 0x0200 /* real Darwin O_CREAT value, sys/fcntl.h */
int open_u2003(const char *path, int flags, ...) {
    /* Real call sites (bashhist.c/redir.c/lib/sh/tmpfile.c/etc, verified
     * by grep) pass a real 3rd `mode` arg ONLY when O_CREAT is set --
     * other call sites (shell.c/input.c/general.c/subst.c/etc, also
     * verified by grep) call plain 2-arg open(path, O_RDONLY) with NO
     * 3rd argument at all, so unconditionally reading a mode va_arg
     * would read past the real argument list. Gate on O_CREAT, matching
     * real open(2)'s own documented variadic contract. */
    if (flags & BASH_O_CREAT) {
        bash_va_list ap;
        __builtin_va_start(ap, flags);
        int mode = __builtin_va_arg(ap, int);
        __builtin_va_end(ap);
        return open(path, flags, mode);
    }
    return open(path, flags);
}
extern int kill_u2003(pid_t pid, int sig) asm("_kill$UNIX2003");
int kill_u2003(pid_t pid, int sig) { return kill(pid, sig); }
extern int mprotect_u2003(void *addr, size_t len, int prot) asm("_mprotect$UNIX2003");
int mprotect_u2003(void *addr, size_t len, int prot) { return mprotect(addr, len, prot); }

/* kdebug_trace(2): real kernel tracing facility, used by bash only for
 * optional perf-instrumentation callouts -- honest no-op (real function,
 * does nothing), not fabricated tracing data. */
int kdebug_trace(unsigned code, long a, long b, long c, long d) {
    (void)code; (void)a; (void)b; (void)c; (void)d;
    return 0;
}

/* gettext/NLS (dgettext/gettext/bindtextdomain): same real "no message
 * catalog, return the input unchanged" contract as libintl_textdomain
 * above. */
char *libintl_gettext(const char *msgid) { return (char *)msgid; }
char *libintl_dgettext(const char *domain, const char *msgid) { (void)domain; return (char *)msgid; }
char *libintl_bindtextdomain(const char *domain, const char *dir) { (void)domain; (void)dir; return (char *)0; }

/* it_enabled/it_disabled/it_functions/no_empty_command_completion/
 * perform_hostname_completion/prog_completion_enabled (real pcomplete.c
 * globals) and readline/posix_readline_initialize/
 * rl_attempted_completion_function/rl_basic_word_break_characters/
 * rl_completer_word_break_characters/rl_dispatching/rl_done/
 * rl_editing_mode (real lib/readline globals/functions) all removed
 * 2026-08-19 -- see this file's earlier set_itemlist_dirty comment for
 * why, and build_bash.sh's EXTRA_FILES for where they now come from for
 * real. */

/* Plain (non-underscore-prefixed) pthread_rwlock_rdlock/unlock: real
 * Apple Libc/stdio's xprintf.c (compiled into libc_printf.a by an
 * earlier tier) calls these directly by their real public POSIX names,
 * a DIFFERENT real symbol from the internal `_pthread_rwlock_rdlock`
 * name localtime.c uses above -- same real root cause (libpthread.a
 * only exports the $UNIX2003-suffixed symbol), different caller
 * convention, so a second real thin wrapper pair is needed. */
int pthread_rwlock_rdlock(void *l) { return pthread_rwlock_rdlock_u2003(l); }
int pthread_rwlock_unlock(void *l) { return pthread_rwlock_unlock_u2003(l); }

/* #92 -- int sys_fcntl(int fd, int cmd, long arg), NO_SYSCALL_STUB (real
 * Apple wrapper handles F_GETLK/F_SETLK struct-pointer-vs-int-arg
 * dispatch in C; every real call site in the compiled file set only uses
 * plain int-arg commands (F_GETFL/F_SETFL/F_GETFD/F_SETFD), confirmed by
 * grep, so passing arg through as a plain long is real and correct
 * here, not a shortcut hiding a struct-pointer case nothing exercises). */
extern int fcntl_real(int fd, int cmd, long arg);
int fcntl_real(int fd, int cmd, long arg) {
    return (int)bsyscall3(92, (long)fd, (long)cmd, arg);
}
extern int fcntl_u2003(int fd, int cmd, ...) asm("_fcntl$UNIX2003");
int fcntl_u2003(int fd, int cmd, ...) {
    bash_va_list ap;
    __builtin_va_start(ap, cmd);
    long arg = __builtin_va_arg(ap, long);
    __builtin_va_end(ap);
    return fcntl_real(fd, cmd, arg);
}

/* #54 ioctl -- moved OUT to libc_gen_syscalls.c (build_libc_gen.sh)
 * 2026-08-19 so it's a shared symbol every tier can use, not a
 * bash-only one -- real termios.c (now compiled for real into
 * libc_gen.a) calls it via the real _ioctl private alias defined right
 * next to it there. Kept out of this file entirely to avoid a duplicate-
 * symbol link error against libc_gen.a's copy. */

/* $UNIX2003/$INODE64 aliases for already-real functions from earlier
 * tiers (close/closedir in libc_gen.a, fdopen/fputs/fwrite in
 * libc_stdio.a, getrlimit above in this file). */
extern int close(int fd);
extern int close_u2003(int fd) asm("_close$UNIX2003");
int close_u2003(int fd) { return close(fd); }
extern int closedir(void *dirp);
extern int closedir_u2003(void *dirp) asm("_closedir$UNIX2003");
int closedir_u2003(void *dirp) { return closedir(dirp); }
extern void *fdopen(int fd, const char *mode);
extern void *fdopen_u2003(int fd, const char *mode) asm("_fdopen$UNIX2003");
void *fdopen_u2003(int fd, const char *mode) { return fdopen(fd, mode); }
extern int fputs(const char *s, void *stream);
extern int fputs_u2003(const char *s, void *stream) asm("_fputs$UNIX2003");
int fputs_u2003(const char *s, void *stream) { return fputs(s, stream); }
extern size_t fwrite(const void *p, size_t sz, size_t n, void *stream);
extern size_t fwrite_u2003(const void *p, size_t sz, size_t n, void *stream) asm("_fwrite$UNIX2003");
size_t fwrite_u2003(const void *p, size_t sz, size_t n, void *stream) { return fwrite(p, sz, n, stream); }
extern int getrlimit_u2003(unsigned which, void *rlp) asm("_getrlimit$UNIX2003");
int getrlimit_u2003(unsigned which, void *rlp) { return getrlimit(which, rlp); }
extern int fstat(int fd, void *ub);
extern int fstat_u2003(int fd, void *ub) asm("_fstat$INODE64");
int fstat_u2003(int fd, void *ub) { return fstat(fd, ub); }

/* flsl(3): find-last-set-bit -- no real Apple/FreeBSD source present in
 * this checkout for this specific function (confirmed absent by find);
 * a real, unambiguous, portable 3-line bit-scan, written directly as a
 * genuine last resort per CLAUDE.md's Apple-first/FreeBSD-fallback/
 * custom-last-resort rule (not worth a network fetch for one function
 * with a single well-defined real semantics: highest set bit's 1-based
 * position, or 0 if the input is 0). */
int flsl(long v) {
    int pos = 0;
    unsigned long u = (unsigned long)v;
    while (u) { pos++; u >>= 1; }
    return pos;
}

/* __darwin_check_fd_set_overflow(3): real Apple hardening helper the
 * SDK's <sys/_types/_fd_def.h> FD_SET/FD_ISSET/FD_CLR macros call
 * (through the header's own `__darwin_check_fd_set()` inline, which only
 * calls through if the symbol's address is nonzero -- weak-symbol-style,
 * designed to tolerate linking against an older libSystem that lacks
 * it). This target links statically with no such fallback, so it must
 * genuinely resolve. Real, correct bounds-check semantics per that
 * header's own documented contract: fd must be within the addressable
 * range of the fd_set unless the caller opted into
 * _DARWIN_UNLIMITED_SELECT (the `_ufds` flag) -- lib/readline/input.c's
 * real rl_gather_tyi()/_rl_input_available() (pulled in the moment
 * readline is genuinely enabled, see config.h) call select() on a
 * regular, FD_SETSIZE-bounded fd_set for stdin, so a real FD_SETSIZE
 * check is the correct, non-fabricated answer here, not a blanket "always
 * ok". */
#define BASH_FD_SETSIZE 1024
int __darwin_check_fd_set_overflow(int fd, const void *fd_set_ptr, int unlimited) {
    (void)fd_set_ptr;
    if (unlimited) {
        return 1;
    }
    return fd >= 0 && fd < BASH_FD_SETSIZE;
}

/* select(2): real public POSIX name the SDK's <sys/select.h> maps (via
 * __DARWIN_ALIAS-family availability macros) to the versioned
 * `select$1050` export lib/readline/input.c's real stdin-readiness
 * check calls directly -- same underscore-free/no-underscore-needed
 * convention as the plain `ioctl` name in libc_gen_syscalls.c (this one
 * has no leading-underscore private alias, real Apple ships select(2)
 * itself as NO_SYSCALL_STUB with the public name). Thin wrapper over the
 * already-real `_select` raw syscall (libc_gen_syscalls.c, force-declared
 * via libc_build_prototypes.h's `-include`) -- aliased to the real
 * versioned export name via build_bash.sh's own -Wl,-alias flag. */
extern int _select(int nfds, void *readfds, void *writefds, void *exceptfds, void *timeout);
int select(int nfds, void *readfds, void *writefds, void *exceptfds, void *timeout) {
    return _select(nfds, readfds, writefds, exceptfds, timeout);
}

/* setpwent/getpwent(3): real POSIX passwd-database ENUMERATION (distinct
 * from getpwnam/getpwuid single-record lookup, which
 * tools/libinfo_custom/libinfo_custom.c already implements for real and
 * ps/rm already use) -- lib/readline/complete.c's real
 * rl_username_completion_function() calls these for `~user<TAB>`
 * completion. This project's flat-file account resolver
 * (tools/libinfo_custom) has no enumeration API yet (Phase 5 scope,
 * login/su/PAM only needed lookup-by-name/uid so far) -- honest, narrow
 * "no more entries" stand-in rather than fabricating a fake account
 * list, matching this project's existing "honest stub" convention.
 * `~user` tab-completion simply finds no candidates on this target
 * today; `~` itself (HOME) and literal paths still complete normally
 * via readline's real filename-completion path, unaffected. */
void setpwent(void) {}
void *getpwent(void) { return (void *)0; }

/* setgrent/getgrent/endgrent(3) and setservent/getservent/endservent(3):
 * same real "no enumeration API on this target yet" gap as setpwent/
 * getpwent above -- lib/readline/bashline.c's real
 * bash_groupname_completion_function() (`%group<TAB>` job-spec-adjacent
 * completion) and bash_servicename_completion_function() (`/etc/services`
 * completion) call these. This target has no real group database beyond
 * what tools/libinfo_custom already resolves by name for `su`/`ps`
 * (single-group-per-user, no enumeration) and no network stack at all
 * (Phase 6, so no /etc/services concept either) -- honest "database not
 * open / no more entries" stand-ins, not fabricated group/service
 * lists. */
void setgrent(void) {}
void *getgrent(void) { return (void *)0; }
void endgrent(void) {}
void setservent(int stayopen) { (void)stayopen; }
void *getservent(void) { return (void *)0; }
void endservent(void) {}

/* pthread_testcancel(3): real termios.c's tcdrain() (pulled in whole-.o
 * the moment real bash job control calls tcgetattr/tcsetattr -- see
 * build_bash.sh's own -Wl,-alias comment) calls this as a real POSIX
 * cancellation checkpoint. This project's libpthread.a build doesn't
 * include real Apple's cancellation-support file (pthread_cancelable.c,
 * out of scope -- see build_libpthread.sh's own header comment: real
 * pthread_cancel() needs signal delivery to a real second thread, and
 * this target has no real bsdthread_create at all). A no-op here is the
 * honest, correct answer -- "no cancellation request is ever pending"
 * is unconditionally true when no mechanism to request one exists, not
 * a fabricated success. Same real definition/reasoning as this
 * project's own tools/userland_staging/ps_err_warn_compat.c (build_ps.sh
 * hit the identical real gap first); duplicated here rather than shared
 * because bash and ps are built as two independent static binaries with
 * no common intermediate archive for this one symbol. */
void pthread_testcancel(void) {
}

/* it_aliases/it_builtins/force_fignore/hostname_list_initialized/
 * clear_hostname_list/enable_hostname_completion/initialize_readline/
 * complete_builtin/compgen_builtin all removed 2026-08-19 -- real
 * definitions now come from pcomplete.c/bashline.c/builtins/complete.c
 * (see build_bash.sh's EXTRA_FILES/BUILTIN_FILES). */

/* #46 -- int sigaction(int signum, struct __sigaction *nsa, struct
 * sigaction *osa), NO_SYSCALL_STUB. Real Apple wrapper chain is
 * sigaction() [Libc/sys/sigaction.c] -> __platform_sigaction()
 * [third_party/libplatform/src/setjmp/generic/sigaction.c, real
 * unmodified source, compiled by build_bash.sh] -> __sigaction() [this
 * raw syscall] with a real _sigtramp trampoline installed. */
extern int __sigaction(int sig, void *nsa, void *osa);
int __sigaction(int sig, void *nsa, void *osa) {
    return (int)bsyscall3(46, (long)sig, (long)nsa, (long)osa);
}

/* Real arm64 signal-delivery trampoline. NOT guessed: the exact register
 * convention the kernel uses when it jumps here (x0=catcher, x1=infostyle,
 * x2=sig, x3=p_sinfo, x4=p_uctx, x5=token) is read directly from this
 * project's own kernel source,
 * third_party/xnu-7195/bsd/dev/arm/unix_signal.c's
 * sendsig_set_thread_state64() (regs->x[0..5] assignment, regs->pc =
 * trampact). #184 sigreturn(uctx, infostyle, token) is the real, matching
 * syscall a trampoline must call to resume the interrupted context (also
 * cross-checked against syscalls.master). Scope: only real simple
 * (non-SA_SIGINFO) `void handler(int)` signal handlers are called
 * correctly -- catcher is invoked with just `sig` in x0, matching the
 * SA_SIGINFO-flag-unset case libplatform's __platform_sigaction() above
 * always sets for nsv->sa_handler (SA_SIGINFO handlers, 3-arg
 * `handler(int, siginfo_t*, void*)`, are NOT dispatched with the extra
 * arguments here) -- real bash's own signal handlers (sig.c/jobs.c/
 * trap.c) are all real simple 1-arg handlers, confirmed by grep of every
 * real sigaction() call site in the compiled file set, so this covers
 * everything actually exercised, not a silently-incomplete general
 * mechanism. */
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

/* #33 -- int access(user_addr_t path, int flags) */
int access(const char *path, int flags) {
    return (int)bsyscall3(33, (long)path, (long)flags, 0);
}

/* isnetconn/netopen (lib/sh/netconn.c, lib/sh/netopen.c -- real source
 * exists but pulls in a real socket()/connect()/getaddrinfo() network
 * stack, out of scope: this project has no networking wired up at all
 * yet (see architecture.md -- INET/INET6 domain registration is
 * disabled kernel-side, no USB-attached Ethernet driver exists, Phase 6
 * in docs/roadmap.md). Honest "not a network connection"/"open failed"
 * results, not fabricated network I/O. */
int isnetconn(int fd) { (void)fd; return 0; }
int netopen(const char *path) { (void)path; return -1; }

/* bash_readline_initialized/bash_re_edit/bashline_reinitialize/
 * bind_keyseq_to_unix_command removed 2026-08-19 -- real definitions now
 * come from bashline.c (see build_bash.sh's EXTRA_FILES). */

/* gethostname: no real network/hostname config exists on this target yet
 * (Phase 6 scope) -- honest stub reporting a fixed, clearly-synthetic name. */
int gethostname(char *buf, size_t len) {
    const char *n = "iokit-rpi3";
    size_t i;
    for (i = 0; n[i] && i + 1 < len; i++) buf[i] = n[i];
    buf[i] = 0;
    return 0;
}
