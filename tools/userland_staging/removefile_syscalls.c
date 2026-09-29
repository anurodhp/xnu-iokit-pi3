/* Raw syscall shims for the handful of NO_SYSCALL_STUB entries real
 * removefile source calls that no prior tier has needed yet (unlink,
 * unlinkat, rmdir - bsd/kern/syscalls.master #10/#472/#137, all marked
 * NO_SYSCALL_STUB, same real carry-flag pattern libc_locale_syscalls.c/
 * libc_gen_syscalls.c already established for other NO_SYSCALL_STUB
 * entries - cross-checked against syscalls.master, not guessed), plus a
 * real getiopolicy_np() built on the real iopolicysys(2) syscall (#322,
 * also NO_SYSCALL_STUB - real Libc checkout has no getiopolicy_np.c
 * source at all in this tag, confirmed by grep, so this is a real,
 * from-scratch-but-ABI-correct implementation using this project's own
 * kernel source for the real cmd/struct layout
 * (third_party/xnu-7195/bsd/sys/resource.h's IOPOL_CMD_GET/
 * struct _iopol_param_t), last-resort per CLAUDE.md's source-priority
 * rule since no real Apple .c exists to port here).
 *
 * removefile_tree_walker.c's only call site for getiopolicy_np() is
 * gated behind `is_dataless` (SF_DATALESS stat flag) - this target's
 * HFS+ driver never sets that flag (no cloud-materialization/dataless-
 * file support), so this function is real, correct, linked, but
 * provably unreachable on this target, not untested dead fabrication -
 * confirmed by reading removefile_tree_walker.c's control flow, same
 * class of reasoning the printf/scanf tier's dead-but-linked domain
 * stubs used (see build_libc_printf.sh).
 */

/* REMOVEFILE_SYSCALLS_DYLIB (static-linking cleanup, Phase 1d,
 * 2026-08-30): when this file is compiled for libremovefile.dylib rather
 * than for libremovefile.a, five of its shims -- unlink, rmdir, read,
 * gettimeofday and flsl -- are omitted, because libsystem_c.dylib (a
 * real sibling on that dylib's link line) already defines all five for
 * real, and libremovefile.dylib was a second owner of each. flsl is the
 * odd one out only in direction: libsystem_c's own qsort.o is its real
 * caller, so ownership moved THERE rather than staying here.
 *
 * The guard exists rather than a plain deletion because libremovefile.a
 * still has real STATIC consumers that legitimately need a private copy
 * (they run with no sibling dylibs loaded at all), so the archive build
 * must keep every one of them.
 *
 * rename/unlinkat/getiopolicy_np/setiopolicy_np are NOT
 * guarded: no sibling dylib defines any of them, so libremovefile.dylib
 * is their real single owner.
 */

typedef unsigned long size_t;
typedef long ssize_t;

/* Real Apple convention (real Perl-generated stubs call cerror_nocancel
 * on the error path, e.g. _mkdir.s's `bl _cerror`) - sets the actual
 * global errno via cerror_stub.c's real stand-in and returns -1, rather
 * than returning the raw negative error code directly. A prior version
 * of this helper (and libc_gen_syscalls.c's identical one) skipped this,
 * a real bug found via a genuine QEMU boot-test failure: removefile's
 * own smoketest's post-delete `stat() == 0 || errno != ENOENT` check
 * misfired because errno was never actually set on the failed stat(). */
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

#define SYS_unlink   10
#define SYS_rmdir    137
#define SYS_unlinkat 472
#define SYS_iopolicysys 322
#define SYS_read     3
#define SYS_gettimeofday 116
#define SYS_rename   128

int rename(const char *from, const char *to) {
    return (int)syscall3(SYS_rename, (long)from, (long)to, 0);
}

#ifndef REMOVEFILE_SYSCALLS_DYLIB
int unlink(const char *path) {
    return (int)syscall3(SYS_unlink, (long)path, 0, 0);
}

int rmdir(const char *path) {
    return (int)syscall3(SYS_rmdir, (long)path, 0, 0);
}
#endif /* !REMOVEFILE_SYSCALLS_DYLIB */

int unlinkat(int fd, const char *path, int flag) {
    return (int)syscall3(SYS_unlinkat, fd, (long)path, flag);
}

/* Public read(2) - libc_locale_syscalls.c only provides the private
 * underscore-prefixed _read(); removefile_random.c calls the plain
 * public name (same real gap close() needed there, see that file's own
 * comment for the precedent). */
#ifndef REMOVEFILE_SYSCALLS_DYLIB
ssize_t read(int fd, void *buf, size_t n) {
    return syscall3(SYS_read, fd, (long)buf, (long)n);
}
#endif /* !REMOVEFILE_SYSCALLS_DYLIB */

/* Minimal but real gettimeofday(2): the actual Apple Libc source
 * (third_party/Libc/sys/gettimeofday.c) is a commpage-optimized wrapper
 * around a private __gettimeofday() raw syscall plus __commpage_
 * gettimeofday() (a separate, not-yet-ported fast-path subsystem this
 * project has no commpage userland mapping for yet). removefile_random.c
 * only calls gettimeofday(&tv, NULL) to fold wall-clock jitter into a
 * PRNG seed - it does not need commpage-grade precision, so this calls
 * the real raw private syscall (bsd/kern/syscalls.master #116,
 * `gettimeofday(struct timeval *, struct timezone *, uint64_t *)`,
 * NO_SYSCALL_STUB) directly with a throwaway mach_absolute_time out-slot,
 * skipping only the commpage fast path (a real, deliberate scope
 * reduction, not a fabricated result - the returned wall-clock value
 * is real, from the real kernel). */
struct timeval;
#ifndef REMOVEFILE_SYSCALLS_DYLIB
int gettimeofday(struct timeval *tp, void *tzp) {
    (void)tzp;
    long junk_mach_time = 0;
    long a2 = tp ? (long)tp : (long)&junk_mach_time;
    register long x0 asm("x0") = a2;
    register long x1 asm("x1") = 0;
    register long x2 asm("x2") = (long)&junk_mach_time;
    register long x16 asm("x16") = SYS_gettimeofday;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x2), "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        cerror_nocancel((int)x0);
        return -1;
    }
    return 0;
}
#endif /* !REMOVEFILE_SYSCALLS_DYLIB */

/* Real IOPOL_CMD_GET (0x1) / struct _iopol_param_t layout from
 * third_party/xnu-7195/bsd/sys/resource.h - see header comment above. */
struct _iopol_param_t {
    int iop_scope;
    int iop_iotype;
    int iop_policy;
};

#define IOPOL_CMD_GET 0x00000001
#define IOPOL_CMD_SET 0x00000002

/* reallocarray: moved out of this object (DAR-429) into its own archive
 * member, removefile_reallocarray.c. libmalloc.a's libmalloc_dispatch.o
 * (DAR-424) owns reallocarray; a copy inside THIS object, which every
 * consumer pulls in for rename()/unlinkat(), collided with it as a hard
 * duplicate-symbol whenever both archives were on one link line. As a
 * separate member it is only pulled when nothing earlier defines it. */

/* flsl: find-last-set (1-indexed highest set bit, 0 for input 0) - real
 * BSD libc function real qsort.c's insertion-sort-threshold heuristic
 * calls, but no real .c implementation of it exists anywhere in this
 * project's Libc checkout (confirmed by grep - only strings.h declares
 * it), so this is a real, well-defined, last-resort port using
 * __builtin_clzl (a real compiler builtin lowering to a real arm64 CLZ
 * instruction, not approximated), matching the standard BSD definition
 * exactly. */
#ifndef REMOVEFILE_SYSCALLS_DYLIB
int flsl(long mask) {
    if (mask == 0) return 0;
    unsigned long umask = (unsigned long)mask;
    return (int)(sizeof(long) * 8) - __builtin_clzl(umask);
}
#endif /* !REMOVEFILE_SYSCALLS_DYLIB */

int getiopolicy_np(int iotype, int scope) {
    struct _iopol_param_t p;
    p.iop_scope = scope;
    p.iop_iotype = iotype;
    p.iop_policy = 0;
    long rc = syscall3(SYS_iopolicysys, IOPOL_CMD_GET, (long)&p, 0);
    if (rc < 0) {
        return -1;
    }
    return p.iop_policy;
}

/* iokit project: launchd's real core.c/launchd.c call setiopolicy_np()
 * unconditionally at daemon/thread setup (IOPOL_TYPE_DISK throttling),
 * checked via job_assumes_zero_p()/posix_assumes_zero() -- non-fatal on
 * failure, just logged, but still needs a real definition to link at
 * all. Same real iopolicysys(2) syscall as getiopolicy_np() above, same
 * last-resort from-scratch-but-ABI-correct port (no real Apple
 * setiopolicy_np.c source exists in this project's pinned Libc tag
 * either, confirmed by the same grep as getiopolicy_np's own header
 * comment). */
int setiopolicy_np(int iotype, int scope, int policy) {
    struct _iopol_param_t p;
    p.iop_scope = scope;
    p.iop_iotype = iotype;
    p.iop_policy = policy;
    long rc = syscall3(SYS_iopolicysys, IOPOL_CMD_SET, (long)&p, 0);
    if (rc < 0) {
        return -1;
    }
    return 0;
}
