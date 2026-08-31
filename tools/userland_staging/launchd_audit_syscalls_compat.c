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
 * needing real Mach clock APIs this project hasn't wired for this
 * purpose. Real Apple's own simple/no-cancel-point case reduces to
 * exactly this: __semwait_signal(0, 0, 1, 1, tv_sec, tv_nsec) with no
 * real semaphores involved -- a plain relative timed wait, matching the
 * real raw syscall (#334, bsd/kern/syscalls.master) directly rather
 * than porting the whole file for launchd's simple retry-backoff use
 * (core.c). Doesn't fill in *remaining_time on early wake (real
 * callers here pass NULL for it). */
int nanosleep(const struct timespec *req, struct timespec *rem) {
    (void)rem;
    long rc = syscall6(SYS_semwait_signal, 0, 0, 1, 1, req->tv_sec, req->tv_nsec);
    return (rc < 0) ? -1 : 0;
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
