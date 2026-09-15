/* iokit project: real gaps found booting launchd as PID 1 for the first
 * time (2026-08-28, docs/daemon-roadmap.md Phase 6.8 Milestone 4/5), via
 * a real dyld "Symbol not found" abort per missing symbol.
 *
 * audit_session_self()/audit_session_port()/audit_session_join(): real
 * BSM audit-session Mach RPCs. No real userland client source for these
 * exists anywhere in this project's pinned repos (checked: not in
 * third_party/BSM, not in Libc -- only the KERNEL-side implementation,
 * third_party/xnu-7195/bsd/security/audit/audit_session.c, which is a
 * different, non-portable side of this API). A full port would need a
 * real Mach special-port lookup this project has no infrastructure for
 * yet. Every real call site in PureDarwin's launchd.c/core.c already
 * tolerates failure gracefully -- audit_session_port() is explicitly
 * checked against errno==ENOSYS as an accepted outcome, and the other
 * two are wrapped in job_assumes()/job_assumes_zero_p() (log-only, not
 * fatal) -- so honest no-op/ENOSYS stands in here rather than a fake
 * implementation, matching this project's own "clearly-documented,
 * provable architectural gap" convention (see CLAUDE.md).
 *
 * getpeername()/gethostuuid(): both real BSD syscalls (bsd/kern/
 * syscalls.master #31/#142), both marked NO_SYSCALL_STUB -- meaning
 * real Apple's build generates the raw trap wrapper (__getpeername/
 * __gethostuuid) via a mechanism this project doesn't have (a separate,
 * hand-written .s file real Apple ships outside this pinned libsyscall
 * checkout). Same real, direct-raw-syscall pattern already established
 * for readlink/unlink/rmdir/rename in removefile_syscalls.c -- real
 * syscall numbers cross-checked against syscalls.master, not guessed.
 * gethostuuid() here is the plain, no-fallback SPI shape (real Apple's
 * own gethostuuid.c wraps this with a _gethostuuid_callback fallback
 * launchd itself never installs, so the extra layer isn't needed).
 */
#include <mach/mach.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <gethostuuid.h>
#include <bsm/audit.h>
#include <time.h>
#include <errno.h>

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

static inline long syscall6(long num, long a0, long a1, long a2, long a3, long a4, long a5) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x3 asm("x3") = a3;
    register long x4 asm("x4") = a4;
    register long x5 asm("x5") = a5;
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        return cerror_nocancel((int)x0);
    }
    return x0;
}

#define SYS_getpeername 31
#define SYS_gethostuuid 142
#define SYS_readlink 58
#define SYS_semwait_signal 334

ssize_t readlink(const char *path, char *buf, size_t bufsiz) {
    return (ssize_t)syscall3(SYS_readlink, (long)path, (long)buf, (long)bufsiz);
}

/* nanosleep(3): real Apple source (Libc/gen/nanosleep.c) is a large,
 * dual-path implementation (mk_timer-based vs __semwait_signal-based)
 * needing real Mach clock APIs. This is the reduced, no-cancel-point
 * case of it -- but it MUST still pass a real semaphore port.
 *
 * FIXED 2026-09-15 (DAR-309). This previously passed 0 for BOTH
 * cond_sem and mutex_sem, with a comment asserting real Apple's simple
 * case "reduces to exactly this ... with no real semaphores involved".
 * That claim was FALSE, and the real pinned sources disprove it on both
 * ends:
 *
 *   - Real Apple (third_party/Libc/gen/nanosleep.c:104) passes a REAL
 *     semaphore as cond_sem and MACH_PORT_NULL only as mutex_sem:
 *       SEMWAIT_SIGNAL(clock_sem, MACH_PORT_NULL, 1, 1, sec, nsec)
 *     where clock_sem is created at that file's own line 55 via
 *     semaphore_create(mach_task_self(), &clock_sem, SYNC_POLICY_FIFO, 0).
 *
 *   - Real kernel (xnu-7195): with mutex_sem==0 and timeout==1,
 *     __semwait_signal_nocancel() (bsd/kern/kern_sig.c:1232) calls
 *     semaphore_timedwait_trap_internal(cond_sem, ...), which calls
 *     port_name_to_semaphore(name, ...) (osfmk/kern/sync_sema.c:945).
 *     That returns KERN_INVALID_NAME immediately for an invalid name
 *     (osfmk/kern/ipc_sync.c:63, !MACH_PORT_VALID(0)) WITHOUT ever
 *     reaching semaphore_wait_internal().
 *
 * So cond_sem==0 never blocked at all -- it failed instantly, every
 * call. That is the real root cause of DAR-309's ~100% PID-1 CPU: every
 * caller that thought it was sleeping was actually spinning. Real fix is
 * Apple's own: a real semaphore, which mach_ipc_clock_init() already
 * creates here exactly the way Apple's _init_clock_port() does, and
 * which simple_cmds_nanosleep_compat.c already uses correctly.
 *
 * Note ETIMEDOUT is the NORMAL success path (the timeout expired), same
 * as real Apple's own nanosleep.c:106. Doesn't fill in *remaining_time
 * on early wake (real callers here pass NULL for it), same as before. */
extern void mach_ipc_clock_init(void);
extern mach_port_t g_mach_ipc_clock_sem;

int nanosleep(const struct timespec *req, struct timespec *rem) {
    (void)rem;
    if (req == NULL || req->tv_sec < 0 || req->tv_nsec >= 1000000000L) {
        errno = EINVAL;
        return -1;
    }
    mach_ipc_clock_init();
    long rc = syscall6(SYS_semwait_signal, (long)g_mach_ipc_clock_sem, 0, 1, 1,
        req->tv_sec, req->tv_nsec);
    if (rc < 0) {
        /* Real relative timed wait that ran to completion reports
         * ETIMEDOUT -- real success, per real Apple nanosleep.c:106. */
        if (errno == ETIMEDOUT) {
            return 0;
        }
        return -1;
    }
    return 0;
}

int getpeername(int socket, struct sockaddr * __restrict address, socklen_t * __restrict address_len) {
    return (int)syscall3(SYS_getpeername, (long)socket, (long)address, (long)address_len);
}

int gethostuuid(uuid_t uuid, const struct timespec *timeoutp) {
    return (int)syscall3(SYS_gethostuuid, (long)uuid, (long)timeoutp, 0);
}

/* asctime()/asctime_r(): the hand-rolled, locale-independent stand-in
 * that used to live here was REMOVED 2026-08-31 (static-linking
 * cleanup, locale/timezone-string consolidation batch, closing
 * DAR-134). Real blocker was exactly what the comment above used to say:
 * asctime_r() calls strftime_l() just to format the 4-digit year, and
 * this project had no strftime_l() anywhere. `build_libsystem_c.sh` now
 * compiles a real `strftime.o` (real FreeBSD source, for the unrelated
 * locale/timezone-string consolidation this same session did) plus its
 * own real `asctime.o`, so `libsystem_c.dylib` -- already a real,
 * direct sibling on `launchd.macho`'s own link line (see
 * build_launchd.sh) -- now exports the genuine `_asctime`/`_asctime_r`.
 * This file's own hand-rolled copy was force_loaded into launchd.macho
 * right alongside that sibling, so it was shadowing the real
 * implementation with a second, private one -- the same "local define
 * shadows a loaded dylib's real export" class this whole sweep exists to
 * close (confirmed via `dup_symbol_audit.sh --explain`, which already
 * flagged launchd.macho for the adjacent `_localtime` case). Removing it
 * lets launchd.macho's own undefined `_asctime`/`_asctime_r` references
 * (core.c's jobmgr_shutdown/_remove) resolve to the one real owner. */

mach_port_name_t audit_session_self(void) {
    return MACH_PORT_NULL;
}

int audit_session_port(au_asid_t asid, mach_port_name_t *port) {
    (void)asid;
    (void)port;
    extern int *__error(void);
    *__error() = 78; /* ENOSYS -- explicitly tolerated by every real caller */
    return -1;
}

au_asid_t audit_session_join(mach_port_name_t port) {
    (void)port;
    return 0;
}
