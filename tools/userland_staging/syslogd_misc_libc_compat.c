/* DAR-24 pass, 2026-09-03: this file used to carry 23 more local
 * definitions (___sigaction/__sigtramp, atol/atoll, creat, usleep,
 * setiopolicy_np, pipe, shm_open, kqueue, readlink, recvfrom, strtoll,
 * strtoul, os_fault_with_payload, gmtime_r/localtime_r/mktime/timegm/
 * ctime_r + their days_from_civil/civil_from_days helpers, rename,
 * unlink, setsockopt, sendto, pututxline) written when syslogd's own
 * static libc stack genuinely had no other source for them. All 23 are
 * now real exports of a dylib syslogd.macho actually loads --
 * ___sigaction/__sigtramp from libsystem_kernel.dylib (DAR-164's
 * sigtramp_compat.o, written to fix this exact process), atol/strtoll/
 * strtoul/unlink/gmtime_r/localtime_r/mktime/timegm/ctime_r from
 * libsystem_c.dylib (the last five via the real Apple/FreeBSD
 * stdtime/FreeBSD/localtime.c now merged in -- a real, TZ-database-
 * capable implementation, not this file's old "treat local as UTC"
 * stand-in), creat/usleep/shm_open/os_fault_with_payload from
 * libsystem_notify.dylib's own compat file, pipe/kqueue/recvfrom from
 * libsystem_kernel.dylib, readlink from libcopyfile.dylib, rename/
 * setiopolicy_np from libremovefile.dylib (setiopolicy_np via a real
 * iopolicysys(2) call, not this file's old no-op), setsockopt/sendto
 * from libresolv.dylib, and pututxline from libopenpam.dylib's own
 * date_compat.o. Keeping local copies alongside real dylib owners was
 * exactly DAR-24's bug class: two independently-compiled definitions of
 * process-global-ish state/logic in one process. Deleted rather than
 * `-Wl,-U`-deferred, so the reference binds directly to the dylib that
 * already ships it. See tools/userland_staging/dup_symbol_audit.sh.
 *
 * What's left below is real: two raw syscalls with no dylib owner
 * anywhere in this project's dylib graph (mmap, kdebug_trace, both
 * confirmed via `dyld_info -exports` across every shipped dylib, not
 * guessed), the public single-underscore sigaction() forwarder
 * (distinct from the raw ___sigaction trap removed above, and likewise
 * not provided by any loaded dylib), and atoll -- libsystem_c.dylib
 * exports atol (real dylib owner, removed above) but genuinely has no
 * atoll, same "confirmed absent, not guessed" check. */

typedef unsigned long size_t;
typedef long ssize_t;

extern long long strtoll(const char *nptr, char **endptr, int base);

long long atoll(const char *nptr) {
    return strtoll(nptr, (char **)0, 10);
}

struct sigaction_compat;
extern int sigaction(int sig, const struct sigaction_compat *act, struct sigaction_compat *oact);
int _sigaction(int sig, const struct sigaction_compat *act, struct sigaction_compat *oact) {
    return sigaction(sig, act, oact);
}

extern long cerror_nocancel(int err);

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

#define SYS_kdebug_trace 180
int kdebug_trace(unsigned int code, long arg1, long arg2, long arg3, long arg4) {
    return (int)syscall5(SYS_kdebug_trace, code, arg1, arg2, arg3, arg4);
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

#define SYS_mmap 197
void *mmap(void *addr, size_t len, int prot, int flags, int fd, long pos) {
    return (void *)syscall6(SYS_mmap, (long)addr, (long)len, prot, flags, fd, pos);
}
