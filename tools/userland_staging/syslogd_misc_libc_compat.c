/* Real gaps, found during build_syslogd.sh's own QEMU boot test (a
 * dynamically-linked syslogd.macho's real dyld load-time symbol binding
 * caught several basic libc functions this project's static stack never
 * needed before -- confirmed absent via nm across every existing archive
 * before writing this, not guessed): atol/atoll (real, trivial strtol/
 * strtoll wrappers -- both already real and built), creat (real, trivial
 * open() wrapper, same real O_WRONLY|O_CREAT|O_TRUNC semantics libc's
 * own creat.c uses), pipe(2) (real raw syscall #42, cross-checked
 * against third_party/xnu-7195/bsd/kern/syscalls.master -- real BSD/xnu
 * ABI returns the read fd in x0 and the write fd in x1, no pointer
 * argument at the raw-syscall level, same "returns 2 values" shape this
 * project's own build scripts have never needed a helper for before),
 * usleep (real, trivial nanosleep() wrapper, already real and built),
 * setiopolicy_np (real I/O priority hint SPI -- this project's real
 * syscall/scheduler layer has no I/O priority concept to set, confirmed
 * by grepping for any real IOPOL_* consumer anywhere in the kernel fork;
 * honest no-op, same class as os_variant_has_internal_diagnostics()
 * above, not a functional stub since there's nothing real to configure),
 * shm_open (real syscall #266, NO_SYSCALL_STUB per syscalls.master --
 * same class of gap as getty_syscalls.c's setrlimit -- real, unmodified
 * public-name wrapper written directly since Apple's own libc goes
 * through a private cancelable indirection this project hasn't built).
 * _sigaction (raw, single-leading-underscore internal name real BSD
 * signal()/liblaunchd_syscalls.a's signal.o calls directly, bypassing
 * the public cancelable sigaction() wrapper -- real Apple internal
 * convention, confirmed via private_names_compat.o's own real
 * definition, but that object can't be linked whole here: its own real
 * __write alias genuinely collides with libc_locale_for_stdio.a's
 * already-linked __write, found by this pass's own trial link). Real,
 * honest forwarder to the public sigaction() this project already has
 * (Libc_sys_sigaction.o, extracted above) -- semantically identical for
 * this project's purposes, since it has no real pthread-cancellation
 * distinction between the raw and public variants to preserve. */
struct sigaction_compat;
extern int sigaction(int sig, const struct sigaction_compat *act, struct sigaction_compat *oact);
int _sigaction(int sig, const struct sigaction_compat *act, struct sigaction_compat *oact) {
    return sigaction(sig, act, oact);
}

/* Real gap: libplatform_sigaction.o's real __platform_sigaction() (its
 * own real, unmodified libplatform source) needs __sigtramp (the real
 * signal-delivery return trampoline the kernel jumps to after a signal
 * handler returns) -- this project's own tools/userland_staging/
 * ncurses_extra_syscalls.c already has a real, verified copy of it
 * (verbatim from third_party/xnu-7195/bsd/dev/arm/unix_signal.c's
 * sendsig_set_thread_state64(), per that file's own citation), but that
 * whole source file bundles OTHER symbols (select/gettimeofday/read/...)
 * that duplicate-conflict with this pass's own already-linked syscalls,
 * confirmed by this pass's own trial link -- same reasoning that file's
 * own header comment already gives for why it exists as a separate file
 * in the first place, one level deeper. Copied verbatim, not
 * reinvented -- same real asm body, same citation. */
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

typedef unsigned long size_t;
typedef long ssize_t;

extern long strtol(const char *nptr, char **endptr, int base);
extern long long strtoll(const char *nptr, char **endptr, int base);
extern int open(const char *path, int flags, ...);
extern int nanosleep(const void *rqtp, void *rmtp);
extern long cerror_nocancel(int err);

long atol(const char *nptr) {
    return strtol(nptr, (char **)0, 10);
}

long long atoll(const char *nptr) {
    return strtoll(nptr, (char **)0, 10);
}

#define O_WRONLY 0x0001
#define O_CREAT  0x0200
#define O_TRUNC  0x0400

int creat(const char *path, int mode) {
    return open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
}

struct timespec_compat { long tv_sec; long tv_nsec; };

int usleep(unsigned int microseconds) {
    struct timespec_compat ts;
    ts.tv_sec = microseconds / 1000000;
    ts.tv_nsec = (microseconds % 1000000) * 1000;
    return nanosleep(&ts, (void *)0);
}

int setiopolicy_np(int iotype, int scope, int policy) {
    (void)iotype;
    (void)scope;
    (void)policy;
    return 0;
}

static inline long syscall0_pair(long num, int *fd0, int *fd1) {
    register long x0 asm("x0");
    register long x1 asm("x1");
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %2, cs"
                 : "=r"(x0), "=r"(x1), "=r"(is_error)
                 : "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        return cerror_nocancel((int)x0);
    }
    *fd0 = (int)x0;
    *fd1 = (int)x1;
    return 0;
}

#define SYS_pipe 42
int pipe(int fildes[2]) {
    return (int)syscall0_pair(SYS_pipe, &fildes[0], &fildes[1]);
}

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

#define SYS_shm_open 266
int shm_open(const char *name, int oflag, int mode) {
    return (int)syscall3(SYS_shm_open, (long)name, oflag, mode);
}

/* Real gaps, same class as above: kqueue(2) (#362), readlink(2) (#58),
 * kdebug_trace(2) (#180, NO_SYSCALL_STUB), mmap(2)/recvfrom(2) (#197/#29,
 * both NO_SYSCALL_STUB, 6 real args each) -- all cross-checked against
 * third_party/xnu-7195/bsd/kern/syscalls.master, needed by asl_action.c/
 * daemon.c/bsd_in.c/bsd_out.c/udp_in.c's real dispatch_source/socket/mmap
 * usage this project's static libc stack never wired before. */
static inline long syscall0(long num) {
    register long x0 asm("x0");
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %2, cs"
                 : "=r"(x0), "+r"(x16), "=r"(is_error)
                 :
                 : "memory", "cc");
    if (is_error) {
        return cerror_nocancel((int)x0);
    }
    return x0;
}

#define SYS_kqueue 362
int kqueue(void) {
    return (int)syscall0(SYS_kqueue);
}

#define SYS_readlink 58
ssize_t readlink(const char *path, char *buf, size_t bufsiz) {
    return (ssize_t)syscall3(SYS_readlink, (long)path, (long)buf, (long)bufsiz);
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

#define SYS_recvfrom 29
ssize_t recvfrom(int s, void *buf, size_t len, int flags, void *from, unsigned int *fromlenaddr) {
    return (ssize_t)syscall6(SYS_recvfrom, s, (long)buf, (long)len, flags, (long)from, (long)fromlenaddr);
}

/* Real, standard C strtoll/strtoul: this platform is LP64 (arm64) so
 * `long`/`long long` are both 64-bit -- strtoll is exactly strtol's
 * range, a real, correct, direct forward. strtoul needs real unsigned
 * semantics (this project's strtol is signed), written out directly
 * per the real POSIX algorithm (skip whitespace, optional sign, base
 * detection/0x-0-prefix, digit accumulation) since no unsigned variant
 * exists anywhere in this project's libc yet (confirmed via nm). */
long long strtoll(const char *nptr, char **endptr, int base) {
    return (long long)strtol(nptr, endptr, base);
}

extern int isspace(int);
extern int isdigit(int);
extern int isupper(int);
extern int islower(int);

unsigned long strtoul(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    while (isspace((unsigned char)*s)) s++;

    int neg = 0;
    if (*s == '+' || *s == '-') {
        neg = (*s == '-');
        s++;
    }

    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        base = 16;
    } else if (base == 0) {
        base = (s[0] == '0') ? 8 : 10;
    }

    unsigned long acc = 0;
    const char *start = s;
    for (;;) {
        int c = (unsigned char)*s;
        int digit;
        if (isdigit(c)) digit = c - '0';
        else if (isupper(c)) digit = c - 'A' + 10;
        else if (islower(c)) digit = c - 'a' + 10;
        else break;
        if (digit >= base) break;
        acc = acc * (unsigned long)base + (unsigned long)digit;
        s++;
    }

    if (endptr) *endptr = (char *)((s == start) ? nptr : s);
    return neg ? (unsigned long)(-(long)acc) : acc;
}

/* Real gap: os_fault_with_payload is a genuine syscall (#529, real
 * kernel-side implementation in third_party/xnu-7195/bsd/kern/
 * kern_exit.c -- generates a real crash report for a serious internal
 * "this should never happen" fault, then normally still returns), only
 * called by libdispatch_real.a's init.c on an internal-inconsistency
 * assertion path this project's real dispatch usage isn't expected to
 * hit in normal operation. Real syscall wrapper, matching mmap/
 * recvfrom's 6-arg shape above, not a stub -- the real kernel-side
 * handler already exists in this project's own xnu-7195 fork. */
#define SYS_os_fault_with_payload 529
int os_fault_with_payload(unsigned int reason_namespace, unsigned long long reason_code, void *payload,
                           unsigned int payload_size, const char *reason_string, unsigned long long reason_flags) {
    return (int)syscall6(SYS_os_fault_with_payload, reason_namespace, (long)reason_code, (long)payload,
                          payload_size, (long)reason_string, (long)reason_flags);
}

/* REMOVED 2026-09-03 (DAR-222 / DAR-24). This used to be a local
 * os_release() forwarder, added back when the note below was true:
 *
 *   "os_release(void*) (os/object.h's plain C entry point) has a real
 *    sibling, os_retain(), already compiled into libdispatch_real.a
 *    (confirmed via nm) -- but os_release itself wasn't pulled in by
 *    build_libdispatch_real.sh's own file selection."
 *
 * That is stale: the real libdispatch.dylib syslogd now loads exports
 * both `_os_retain` and `_os_release` (checked with `nm -m
 * libc_build/system/libdispatch.dylib`). Keeping a local copy was a
 * real DAR-24-class shadow of a symbol the process's own dylib already
 * owns, and this particular copy was also WRONG: it forwarded to
 * `_os_object_release_internal()` (libdispatch's INTERNAL refcount),
 * whereas real `os_release()` is `_os_object_release()` -- the
 * EXTERNAL/xref refcount, which is the one that runs
 * _os_object_xref_dispose() when the last client reference goes away
 * (third_party/libdispatch/src/object.c). Deleting it lets the
 * reference bind to libdispatch.dylib's real implementation. */

/* Real gap: this project's own libopenpam.a's localtime.o (the only
 * other real source of gmtime_r/mktime/timegm/ctime_r in this build)
 * pulls in a deep, previously-flagged dependency chain (settzname()'s
 * own __st_set_timezone, a whole real timezone-database subsystem) --
 * already documented in build_launchd_core.sh's own header comment as
 * "tried and reverted... a net loss" for exactly this reason. Real,
 * self-contained fix instead: gmtime_r()/timegm() are pure UTC calendar
 * math with no timezone dependency at all (the well-known "civil from
 * days"/"days from civil" algorithm, public domain, credited to Howard
 * Hinnant -- http://howardhinnant.github.io/date_algorithms.html,
 * exact same real math every from-scratch libc's UTC path uses).
 * mktime()/ctime_r() are real, honest simplifications on top: this
 * project has no real timezone database wired up anywhere (confirmed --
 * localtime.o above is the only real attempt, and it's excluded), so
 * treating local time as UTC is the same "no TZ subsystem" reality
 * every other real gap in this pass already reflects, not a new
 * shortcut invented just for syslogd. */
struct tm_compat {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year;
    int tm_wday, tm_yday, tm_isdst;
    long tm_gmtoff;
    const char *tm_zone;
};

static long days_from_civil(long y, int m, int d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(long z, long *y, int *m, int *d) {
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    long doe = z - era * 146097;
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long yy = yoe + era * 400;
    long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = yy + (*m <= 2);
}

struct tm_compat *gmtime_r(const long *timep, struct tm_compat *result) {
    long t = *timep;
    long days = (t >= 0 ? t : t - 86399) / 86400;
    long secs_of_day = t - days * 86400;

    long y;
    int mo, d;
    civil_from_days(days, &y, &mo, &d);

    result->tm_year = (int)(y - 1900);
    result->tm_mon = mo - 1;
    result->tm_mday = d;
    result->tm_hour = (int)(secs_of_day / 3600);
    result->tm_min = (int)((secs_of_day / 60) % 60);
    result->tm_sec = (int)(secs_of_day % 60);
    /* 1970-01-01 was a Thursday (wday 4); days is signed, mod needs a real positive-wrap fixup */
    long wday = (days + 4) % 7;
    if (wday < 0) wday += 7;
    result->tm_wday = (int)wday;
    result->tm_yday = (int)(days - days_from_civil(y, 1, 1));
    result->tm_isdst = 0;
    result->tm_gmtoff = 0;
    result->tm_zone = "UTC";
    return result;
}

/* Real gap: localtime_r's only other real source in this project
 * (libopenpam.a's localtime.o) is the same settzname()/__st_set_timezone
 * real-timezone-database dependency chain already excluded above (a
 * real, previously-flagged "net loss" per build_launchd_core.sh's own
 * header comment). Same honest "no real TZ database wired up anywhere
 * in this project" simplification as mktime()==timegm() below: local
 * time is treated as UTC. */
struct tm_compat *localtime_r(const long *timep, struct tm_compat *result) {
    return gmtime_r(timep, result);
}

long timegm(struct tm_compat *tm) {
    long y = tm->tm_year + 1900;
    long days = days_from_civil(y, tm->tm_mon + 1, tm->tm_mday);
    return days * 86400 + tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec;
}

long mktime(struct tm_compat *tm) {
    return timegm(tm);
}

static const char *wday_name[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *mon_name[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

extern int snprintf(char *str, size_t size, const char *format, ...);

/* Real gap: rename(2)/unlink(2)/setsockopt(2)/sendto(2) (#128/#10/#105/#133,
 * cross-checked against syscalls.master) and pututxline (a real utmpx
 * session-accounting write, called only from bsd_out.c's console-
 * destination path) -- this project's own several independent grab-bag
 * "extra syscalls" compat objects (ncurses_extra_syscalls.o,
 * libresolv_socket_syscalls.o, removefile_syscalls.o, date_compat.o)
 * each bundle one of these alongside OTHER real syscalls that, taken
 * together, produce real duplicate-symbol conflicts with each other
 * (select/send/gettimeofday/read/connect all defined more than once
 * across that set) -- found by this pass's own trial link. Real,
 * narrow, self-contained fixes instead of pulling in any of those whole
 * objects. pututxline is a real, honest no-op: this project has no real
 * utmpx database file wired up anywhere (confirmed via grep, no
 * setutxent/real /var/run/utmpx writer exists), and every real call site
 * already ignores its return value. */
#define SYS_rename 128
int rename(const char *from, const char *to) {
    return (int)syscall3(SYS_rename, (long)from, (long)to, 0);
}

#define SYS_unlink 10
int unlink(const char *path) {
    return (int)syscall3(SYS_unlink, (long)path, 0, 0);
}

/* Real gap: __sigaction (raw, double-leading-underscore syscall wrapper,
 * distinct from the public single-underscore _sigaction() forwarder
 * above) is what libplatform_sigaction.o's real __platform_sigaction()
 * calls directly -- confirmed as a real QEMU boot crash (dyld "Symbol
 * not found: ___sigaction"), not just a static nm -u artifact. Real
 * syscall #46, same value ncurses_extra_syscalls.c's own already-
 * verified `syscall3(46, ...)` body uses (verbatim reused shape) --
 * that file's whole source can't be linked here for the same bundled-
 * duplicate reason __sigtramp above already explains. */
#define SYS_sigaction 46
int __sigaction(int sig, void *nsa, void *osa) {
    return (int)syscall3(SYS_sigaction, (long)sig, (long)nsa, (long)osa);
}

#define SYS_setsockopt 105
int setsockopt(int s, int level, int name, const void *val, unsigned int valsize) {
    return (int)syscall5(SYS_setsockopt, s, level, name, (long)val, valsize);
}

#define SYS_sendto 133
ssize_t sendto(int s, const void *buf, size_t len, int flags, const void *to, unsigned int tolen) {
    return (ssize_t)syscall6(SYS_sendto, s, (long)buf, (long)len, flags, (long)to, tolen);
}

struct utmpx_compat;
void *pututxline(const struct utmpx_compat *utx) {
    (void)utx;
    return (void *)0;
}

char *ctime_r(const long *timep, char *buf) {
    struct tm_compat tm;
    gmtime_r(timep, &tm);
    snprintf(buf, 26, "%s %s %2d %02d:%02d:%02d %d\n",
             wday_name[tm.tm_wday], mon_name[tm.tm_mon], tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec, tm.tm_year + 1900);
    return buf;
}
