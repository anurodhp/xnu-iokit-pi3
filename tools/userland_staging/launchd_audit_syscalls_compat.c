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

/* asctime()/asctime_r(): real FreeBSD source (stdtime/FreeBSD/asctime.c)
 * exists and is pinned, but its own asctime_r() calls strftime_l() just
 * to format a 4-digit year -- a real, large, locale-aware formatter this
 * project doesn't have (see docs/backlog.md item d11 for the full real
 * investigation: isspace_l/toupper_l/__current_locale() etc from the
 * same xlocale.c already link fine now, strftime_l specifically is the
 * one still-missing piece). asctime's output format is fixed by the C
 * standard regardless of locale ("Www Mmm dd hh:mm:ss yyyy\n", exactly
 * 26 bytes with the trailing NUL) -- real callers here (core.c's
 * jobmgr_shutdown/_remove) don't need locale-aware formatting, just this
 * fixed layout, so a small direct implementation is a legitimate last
 * resort (CLAUDE.md source-priority rule), not a functional shortcut. */
static const char *wday_name[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *mon_name[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static char *put2(char *p, int v) {
    p[0] = (char)('0' + (v / 10) % 10);
    p[1] = (char)('0' + v % 10);
    return p + 2;
}

char *asctime_r(const struct tm *timeptr, char *buf) {
    const char *wd = (timeptr->tm_wday >= 0 && timeptr->tm_wday < 7) ? wday_name[timeptr->tm_wday] : "???";
    const char *mo = (timeptr->tm_mon >= 0 && timeptr->tm_mon < 12) ? mon_name[timeptr->tm_mon] : "???";
    char *p = buf;
    p[0] = wd[0]; p[1] = wd[1]; p[2] = wd[2]; p[3] = ' ';
    p[4] = mo[0]; p[5] = mo[1]; p[6] = mo[2]; p[7] = ' ';
    p += 8;
    p = put2(p, timeptr->tm_mday < 0 ? 0 : timeptr->tm_mday);
    *p++ = ' ';
    p = put2(p, timeptr->tm_hour);
    *p++ = ':';
    p = put2(p, timeptr->tm_min);
    *p++ = ':';
    p = put2(p, timeptr->tm_sec);
    *p++ = ' ';
    int year = timeptr->tm_year + 1900;
    if (year < 0) year = 0;
    p[3] = (char)('0' + year % 10); year /= 10;
    p[2] = (char)('0' + year % 10); year /= 10;
    p[1] = (char)('0' + year % 10); year /= 10;
    p[0] = (char)('0' + year % 10);
    p += 4;
    *p++ = '\n';
    *p = 0;
    return buf;
}

static char asctime_buf[26];
char *asctime(const struct tm *timeptr) {
    return asctime_r(timeptr, asctime_buf);
}

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
