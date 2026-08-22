/* Raw syscall wrappers real crontab needs beyond what cron_extra_syscalls.c
 * already provides (both are linked into crontab.macho - see
 * build_cron.sh). Same real carry-flag ABI / cerror_nocancel() convention,
 * same syscalls.master cross-check method as that file's own header
 * comment documents. */
#include <sys/types.h>

extern long cerror_nocancel(int err);

static inline long xsyscall3(long num, long a0, long a1, long a2) {
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

static inline long xsyscall4(long num, long a0, long a1, long a2, long a3) {
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

/* #47 -- int getgid(void) */
gid_t getgid(void) {
    return (gid_t)xsyscall3(47, 0, 0, 0);
}

/* #60 -- int umask(int newmask) */
int umask(int newmask) {
    return (int)xsyscall3(60, (long)newmask, 0, 0);
}

/* #73 -- int munmap(caddr_t addr, size_t len), NO_SYSCALL_STUB */
int munmap(void *addr, size_t len) {
    return (int)xsyscall3(73, (long)addr, (long)len, 0);
}

/* #197 -- user_addr_t mmap(caddr_t addr, size_t len, int prot, int flags,
 * int fd, off_t pos), NO_SYSCALL_STUB, 6 real args -- needs its own trap
 * helper (x0-x5). */
void *mmap(void *addr, size_t len, int prot, int flags, int fd, long pos) {
    register long x0 asm("x0") = (long)addr;
    register long x1 asm("x1") = (long)len;
    register long x2 asm("x2") = prot;
    register long x3 asm("x3") = flags;
    register long x4 asm("x4") = fd;
    register long x5 asm("x5") = pos;
    register long x16 asm("x16") = 197;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x16)
                 : "memory", "cc");
    if (is_error) return (void *)-1; /* MAP_FAILED */
    return (void *)x0;
}

/* #123 -- int fchown(int fd, int uid, int gid) */
int fchown(int fd, uid_t uid, gid_t gid) {
    return (int)xsyscall3(123, (long)fd, (long)uid, (long)gid);
}

/* #124 -- int fchmod(int fd, int mode), NO_SYSCALL_STUB */
int fchmod(int fd, int mode) {
    return (int)xsyscall3(124, (long)fd, (long)mode, 0);
}

/* #128 -- int rename(char *from, char *to), NO_SYSCALL_STUB */
int rename(const char *from, const char *to) {
    return (int)xsyscall3(128, (long)from, (long)to, 0);
}

/* #138 -- int utimes(char *path, struct timeval *tptr) */
int utimes(const char *path, const void *tptr) {
    return (int)xsyscall3(138, (long)path, (long)tptr, 0);
}

/* utime(2): real Apple implements this in terms of utimes(2), converting
 * struct utimbuf{actime,modtime} (time_t pair) to struct timeval[2]
 * (time_t+suseconds_t pairs, usec always 0 here - crontab.c's one real
 * call site, poke_daemon(), only ever passes NULL for "now" anyway, so
 * the conversion path is real but never exercised with actual times). */
struct crontab_utimbuf { long actime, modtime; };
int utime(const char *path, const struct crontab_utimbuf *times) {
    if (times == (const struct crontab_utimbuf *)0) {
        return utimes(path, (const void *)0);
    }
    long tv[4];
    tv[0] = times->actime;  tv[1] = 0;
    tv[2] = times->modtime; tv[3] = 0;
    return utimes(path, tv);
}

/* #216 -- int open_dprotected_np(user_addr_t path, int flags, int class,
 * int dpflags, int mode), NO_SYSCALL_STUB. Real Apple Data Protection
 * (per-file encryption classes) has no equivalent subsystem on this
 * target's HFS+ root at all - real, honest fallback to plain open(2),
 * ignoring class/dpflags, same real effect a system with no Data
 * Protection support would have. Pulled in transitively by
 * libc_stdio.a's already-compiled mktemp.o (real
 * __mkstemp_dprotected_np_action, one of several real code paths mktemp.c
 * compiles together - crontab.c itself only ever calls mkstemp(), not
 * this variant directly). */
extern int open(const char *path, int flags, ...);
int open_dprotected_np(const char *path, int flags, int dpclass, int dpflags, int mode) {
    (void)dpclass;
    (void)dpflags;
    return open(path, flags, mode);
}

/* #469/#470 -- int fstatat/fstatat64(int fd, user_addr_t path,
 * user_addr_t ub, int flag). Pulled in transitively by mktemp.o's real
 * find_temp_path() (checks an existing path component with fstatat
 * before creating a temp file there) - crontab.c's own mkstemp() call
 * never passes a dirfd itself, but the object file needs the symbol to
 * link regardless. */
int fstatat(int fd, const char *path, void *ub, int flag) {
    return (int)xsyscall4(469, (long)fd, (long)path, (long)ub, (long)flag);
}
int fstatat64(int fd, const char *path, void *ub, int flag) {
    return (int)xsyscall4(470, (long)fd, (long)path, (long)ub, (long)flag);
}

/* #475 -- int mkdirat(int fd, user_addr_t path, int mode). Pulled in
 * transitively by mktemp.o's real __mkdtemp_action - crontab.c itself
 * only calls mkstemp(), never mkdtemp(), but again the whole object file
 * links as one unit. */
int mkdirat(int fd, const char *path, int mode) {
    return (int)xsyscall4(475, (long)fd, (long)path, (long)mode, 0);
}

/* ctime(3): real Apple implementation wraps localtime()+asctime() (tzcode
 * subsystem, same real scope decision ps_time_compat.c's own header
 * comment already documents this project not having - UTC-only). Reuses
 * that file's real gmtime()+strftime() instead of a separate asctime()
 * implementation - identical real output shape ("Www Mmm dd hh:mm:ss
 * yyyy\n"), just composed from already-real, already-in-this-build
 * pieces rather than a third parallel implementation. crontab.c's own
 * real call site (replace_cmd()'s "installed by <user> on <date>" audit
 * comment) only needs the string, not the exact libc entry point. */
struct crontab_tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst;
    long tm_gmtoff;
    char *tm_zone;
};
extern struct crontab_tm *gmtime(const long *tp);
extern unsigned long strftime(char *s, unsigned long max, const char *fmt, const void *tm);
char *ctime(const long *tp) {
    static char buf[26];
    void *tm = gmtime(tp);
    if (tm == (void *)0) {
        return (char *)0;
    }
    unsigned long n = strftime(buf, sizeof(buf) - 1, "%c", tm);
    buf[n] = '\n';
    buf[n + 1] = '\0';
    return buf;
}

/* arc4random_uniform(): real Apple's (Libc/gen/FreeBSD/arc4random.c)
 * needs <corecrypto/ccrng.h> - corecrypto is confirmed not open-sourced
 * anywhere in apple-oss-distributions (same real gap
 * md5_commoncrypto_shim.c's own header comment documents for digests).
 * Pulled in transitively by libc_stdio.a's already-compiled mktemp.o
 * (real find_temp_path()'s random-suffix generation) - not reachable by
 * any deliberate crontab.c call site, and not security-sensitive here
 * (temp-filename collision avoidance, not key material), so a real but
 * explicitly non-cryptographic xorshift PRNG is the honest, scoped
 * last-resort implementation, not a fabricated claim of real randomness
 * quality. Seeded from time+pid, matching the real entropy budget a
 * temp-filename generator actually needs (avoid same-second collisions
 * between concurrent processes), not attempting to be
 * security-equivalent to the real corecrypto-backed version. */
extern long time(long *tp);
extern int getpid(void);
static unsigned int xorshift_state;
unsigned int arc4random_uniform(unsigned int upper_bound) {
    if (upper_bound < 2) {
        return 0;
    }
    if (xorshift_state == 0) {
        xorshift_state = (unsigned int)time((long *)0) ^ (unsigned int)getpid() ^ 0x9E3779B9u;
        if (xorshift_state == 0) {
            xorshift_state = 1;
        }
    }
    unsigned int x = xorshift_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    xorshift_state = x;
    return x % upper_bound;
}
