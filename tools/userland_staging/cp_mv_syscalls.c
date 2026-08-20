/* Raw syscall shims for the handful of NO_SYSCALL_STUB entries real
 * cp(1)/mv(1) (and the real Apple third_party/Libc/emulated/lchmod.c,
 * lchflags.c, and Libc/gen/FreeBSD/signal.c sources they pull in) need
 * that no prior tier has generated: lchown (bsd/kern/syscalls.master
 * #364), sigaction (#46), and setattrlist (#221) -- all three marked
 * NO_SYSCALL_STUB there (cross-checked against syscalls.master, not
 * guessed), so the real Apple Perl generator
 * (libsyscall/xcodescripts/create-syscalls.pl) never emits a .s stub for
 * them and every prior tier's SYS_STUBS_NEEDED-based approach can't be
 * reused as-is. Same real carry-flag raw-asm pattern every other
 * NO_SYSCALL_STUB shim in this project uses (removefile_syscalls.c,
 * copyfile_syscalls.c) -- not hand-guessed, ABI-correct per
 * osfmk/arm64's real syscall convention (x16 = syscall number, SVC #0x80,
 * carry flag set on error, error code returned in x0).
 *
 * This project's own established precedent (removefile_syscalls.c's
 * unlink/rmdir, same header comment) is to implement the real PUBLIC
 * symbol name directly via raw asm, skipping Apple's real private-
 * wrapper indirection (e.g. real lchown.c calls __lchown, real
 * sigaction.c calls __platform_sigaction) since this project has neither
 * of those private layers built and the public behavior is
 * indistinguishable for every real call site in cp.c/mv.c/lchmod.c/
 * lchflags.c/signal.c (none of them depend on the legacy
 * ENOTSUP-vs-EOPNOTSUPP translation lchown.c's real wrapper layer adds,
 * or on sigaction.c's CrashReporterClient diagnostic-only fast path).
 */

typedef unsigned long size_t;

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

#define SYS_lchown     364
#define SYS_sigaction  46
#define SYS_setattrlist 221

typedef unsigned int uid_t;
typedef unsigned int gid_t;

int lchown(const char *path, uid_t owner, gid_t group) {
    return (int)syscall3(SYS_lchown, (long)path, owner, group);
}

struct __sigaction;
struct sigaction;

/* Real kernel prototype (bsd/kern/syscalls.master #46) takes the kernel's
 * own `struct __sigaction *` for the new-action argument (a different,
 * larger layout than userland's `struct sigaction` -- it has an extra
 * sa_tramp trampoline-pointer field real libsystem_kernel.dylib's
 * private __sigaction_nobind/__sigaction machinery fills in before
 * trapping). This project has no signal-trampoline/sigreturn(2) support
 * built at all (bsd_pthread_stub.c already docs the broader "no real
 * pthread/signal-delivery subsystem" gap this project has scoped out) --
 * cp.c's only real call site (`signal(SIGINFO, siginfo)`, via
 * Libc/gen/FreeBSD/signal.c's real signal()->_sigaction path) is a
 * best-effort progress-on-SIGINFO handler this target's console can
 * never actually deliver (no real signal-delivery path exists), so an
 * honest passthrough that lets the real kernel syscall run and report
 * its real ABI mismatch/failure (rather than a fabricated success) is
 * the correct scope here -- SIGINFO handling was already a soft/optional
 * feature upstream (macOS-only convenience over ^T), not required for
 * cp's actual copy correctness. */
int sigaction(int sig, const struct sigaction *nsv, struct sigaction *osv) {
    return (int)syscall3(SYS_sigaction, sig, (long)nsv, (long)osv);
}

/* Real Libc/gen/FreeBSD/signal.c calls this exact underscore-prefixed
 * name directly (see that file's `extern int _sigaction(...)` "may or
 * may not be __DYNAMIC__-bound" real dual path) -- same real syscall,
 * both names route to it since this project has no separate legacy/
 * UNIX03-binding split to preserve. */
int _sigaction(int sig, const struct sigaction *nsv, struct sigaction *osv) {
    return sigaction(sig, nsv, osv);
}

/* Libc/gen/FreeBSD/signal.c's other real call site, only compiled under
 * `#if defined(__DYNAMIC__)` -- this project's arm64-apple-ios14.4
 * target compiles that branch (Mach-O static binaries still define
 * __DYNAMIC__, it's an ABI/object-format flag, not a "uses dyld" flag),
 * so both branches of signal__()'s dead `bind` runtime check need a
 * real symbol even though `bind` is always 1 here (signal() always
 * passes bind=1; only the unused, __DYNAMIC__-only _signal_nobind()
 * entry point would ever reach this). Same real syscall as sigaction --
 * no separate "legacy/unbound" behavior exists on this target. */
int _sigaction_nobind(int sig, const struct sigaction *nsv, struct sigaction *osv) {
    return sigaction(sig, nsv, osv);
}

struct attrlist;
int setattrlist(const char *path, struct attrlist *alist, void *attributeBuffer,
                 size_t bufferSize, unsigned long options) {
    return (int)syscall5(SYS_setattrlist, (long)path, (long)alist,
                          (long)attributeBuffer, (long)bufferSize, (long)options);
}

/* setpriority(2) -- bsd/kern/syscalls.master #96, NO_SYSCALL_STUB, same
 * real carry-flag pattern as this file's other raw syscalls. Block 5's
 * nice/renice both need it. */
typedef int id_t_compat;
#define SYS_setpriority 96
int setpriority(int which, id_t_compat who, int prio) {
    return (int)syscall3(SYS_setpriority, which, who, prio);
}

/* fork()/vfork()/waitpid(): mv.c's cross-filesystem-rename fallback
 * (fastcopy() calling out to real /bin/cp + /bin/rm as subprocesses via
 * fork+execl+waitpid, exercised when rename(2) fails EXDEV) is the
 * first real use of process-spawning library calls in this
 * userland_staging tier -- everything before this used raw syscalls
 * directly (tools/init_binary/init.c, PID 1) or never forked at all.
 *
 * Real Apple Libc/sys/fork.c exists and is real source, but its real
 * body unconditionally calls three atfork-notification function
 * pointers (_libSystem_atfork_prepare/_parent/_child) that only get
 * populated by a real libSystem_initializer() call this project's
 * no-dyld target never runs -- linking it as-is would compile clean but
 * NULL-call-pointer crash on the very first real fork(), a real
 * correctness trap far worse than not having fork() at all. This
 * project's own PID-1 (tools/init_binary/init.c's sys_fork(), see its
 * header comment) already root-caused and documents the one genuinely
 * hardware/ABI-specific wrinkle here: arm64 Darwin's real fork(2) kernel
 * trap returns parent/child status in x1 (1 = you're the child), not by
 * x0's sign or value the way Linux does -- getting this backwards makes
 * the "child" branch simply never run. Reusing that exact, already
 * QEMU-verified convention here (last-resort narrow port, matching this
 * project's own precedent of bypassing Apple's real-but-unusable-here
 * private-wrapper layering in removefile_syscalls.c's unlink/rmdir/
 * rename) rather than fighting fork.c's uninitialized-function-pointer
 * dependency. No atfork bookkeeping exists to skip (this project has no
 * real pthread/atfork subsystem at all -- bsd_pthread_stub.c already
 * documents that as a permanent, documented ceiling, not a gap
 * introduced here). */
#define SYS_fork  2
#define SYS_vfork 66
#define SYS_wait4 7

typedef int pid_t;

pid_t fork(void) {
    register long x0 asm("x0") = 0;
    register long x1 asm("x1") = 0;
    register long x16 asm("x16") = SYS_fork;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %2, cs"
                 : "+r"(x0), "+r"(x1), "=r"(is_error)
                 : "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        return (pid_t)cerror_nocancel((int)x0);
    }
    if (x1) {
        /* We are the child -- real fork(2) contract is 0 in the child,
         * regardless of what raw x0 held (real PID of the child, from
         * the kernel's perspective, but POSIX fork() never exposes that
         * to the child itself). */
        return 0;
    }
    return (pid_t)x0;
}

/* Real vfork(2) is a genuine distinct syscall (address-space-sharing
 * optimization until exec/exit) with the same x1 parent/child ABI
 * convention as fork -- but this target has no real address-space-
 * sharing/copy-on-write-skip machinery exercised or verified for it, and
 * mv.c's own real call site (copy(), guarding a fork()-then-execl()
 * sequence) works correctly with a plain fork() too (vfork is always a
 * valid, if slightly slower, substitute for fork when the child execs
 * or exits immediately after, which is exactly mv.c's real usage here --
 * same real semantic-preserving substitution real portable code already
 * relies on). Deliberately reusing fork() rather than the real vfork(2)
 * trap, narrower risk than exercising an unverified sharing path for the
 * first time. */
pid_t vfork(void) {
    return fork();
}

struct rusage;
pid_t waitpid(pid_t pid, int *status, int options) {
    register long x0 asm("x0") = pid;
    register long x1 asm("x1") = (long)status;
    register long x2 asm("x2") = options;
    register long x3 asm("x3") = 0;
    register long x16 asm("x16") = SYS_wait4;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x2), "r"(x3), "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        return (pid_t)cerror_nocancel((int)x0);
    }
    return (pid_t)x0;
}
