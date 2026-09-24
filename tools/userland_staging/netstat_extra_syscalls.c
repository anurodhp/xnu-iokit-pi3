/* Wrappers real netstat(1) needs that no dylib here exports (checked with
 * nm -gU across the libc_build/system dylibs; connect/send/recv DO come from
 * libsystem_kernel.dylib and are not duplicated here, and bcmp comes from
 * the shared hexdump_bcmp_compat.c).
 *
 * sigsuspend(): POSIX pointer form, exactly as
 *   third_party/xnu-7195/libsyscall/wrappers/sigsuspend-base.c does it
 *   (dereference the mask, or use an empty set for NULL), over trap #111
 *   (bsd/kern/syscalls.master: `int sigsuspend(sigset_t mask)`, by value).
 *   apply_sigsuspend_compat.c's sigsuspend() is the raw by-value form and
 *   would be wrong for netstat's `sigsuspend(&sigset)` calls.
 *
 * __sysctl(): the raw sysctl trap, bsd/kern/syscalls.master #202
 *   (NO_SYSCALL_STUB). Libc's gen/FreeBSD/sysctlnametomib.c (for netstat's
 *   sysctlnametomib()) calls it directly for the {0,3} name-to-OID lookup.
 *   bind_target_audit.sh caught it as a clean-link-but-unresolvable bind
 *   (DAR-217 class) when it was left to libSystem.
 *
 * _sigaction(): Libc's gen/FreeBSD/signal.c (compiled into this link for
 *   signal()) calls the FreeBSD-namespaced _sigaction(). The real function
 *   is libsystem_c.dylib's exported sigaction(); forward to it. A function
 *   rather than a -Wl,-alias: ld64 emits a malformed bind for an alias whose
 *   base is not compiled in the same link (this project's documented
 *   finding).
 *
 * Same carry-flag raw-syscall convention as libresolv_socket_syscalls.c.
 */
typedef unsigned long size_t;

extern long cerror_nocancel(int err);

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

#define SYS_sigsuspend 111
#define SYS_sysctl     202

int sigsuspend(const unsigned int *sigmask_p) {
    unsigned int mask = (sigmask_p != 0) ? *sigmask_p : 0;
    return (int)syscall6(SYS_sigsuspend, (long)mask, 0, 0, 0, 0, 0);
}

int __sysctl(int *name, unsigned int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen) {
    return (int)syscall6(SYS_sysctl, (long)name, (long)namelen, (long)oldp,
        (long)oldlenp, (long)newp, (long)newlen);
}

struct sigaction;    /* opaque here; only forwarded */
extern int sigaction(int, const struct sigaction *, struct sigaction *);
int _sigaction(int sig, const struct sigaction *nsa, struct sigaction *osa) {
    return sigaction(sig, nsa, osa);
}
