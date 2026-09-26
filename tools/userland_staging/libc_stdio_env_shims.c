/* DAR-387 note: libsystem_c.dylib builds this file with
 * LIBC_ENV_SHIM_NO_SYSCONF. It then exports Apple's real
 * Libc/gen/FreeBSD/sysconf.c (getrlimit exists in libsystem_kernel now),
 * and getdtablesize() below makes the real syscall. The original rationale
 * that follows applies only to the static libc_stdio.a (pwtool). */
/* Minimal, honestly-scoped stand-ins for the rlimit/fd-table accessors
 * third_party/Libc/stdio/FreeBSD/findfp.c calls - real behavior would need
 * a real getrlimit(2) plumbed through, which this target's raw-syscall
 * layer doesn't have yet (see docs/roadmap.md Phase 4 for what's ported
 * so far). Same "real missing subsystem, fixed documented constant, not a
 * silently-wrong guess" pattern already used by getenv()/issetugid() in
 * libc_locale_env_stubs.c.
 *
 *   sysconf(_SC_STREAM_MAX): __sinit() uses this to size __stream_max,
 *     the ceiling f_prealloc()/moreglue() enforce for "streams counted
 *     against STREAM_MAX". FOPEN_MAX (third_party/Libc/include/stdio.h's
 *     own real minimum) is the correct conservative answer for a target
 *     with no real per-process fd-limit query yet - it's what stdio
 *     itself already assumes as its baseline, not an arbitrary number.
 *   getdtablesize(): only reachable via the deliberately-unused
 *     f_prealloc() (see build_libc_stdio.sh's header comment - not
 *     called by anything in this project's smoketest or shell), but
 *     still part of findfp.o as a whole linked object, so needs SOME
 *     real symbol. Returns the same FOPEN_MAX-based constant for
 *     consistency with the sysconf stand-in above.
 *
 * This file's own sysconf() is the ONLY sysconf symbol anywhere in this
 * project's built libc archives (third_party/Libc/gen/FreeBSD/sysconf.c,
 * the real general-purpose implementation, was never added to any
 * build_libc_*.sh file list -- confirmed via nm across every archive in
 * libc_build/, not assumed) -- so it silently shadows real callers that
 * need cases beyond _SC_STREAM_MAX, real, reproducible bug found while
 * porting xargs (Phase 4.6): xargs.c's own real main() hard-requires
 * sysconf(_SC_ARG_MAX) to succeed (`errx(1, "sysconf(_SC_ARG_MAX)
 * failed")` if it returns -1, no fallback path). _SC_ARG_MAX is added
 * here rather than pulling in the real sysconf.c wholesale (a much
 * bigger swap that would also replace this exact, already-QEMU-proven
 * stdio-bootstrap _SC_STREAM_MAX path for every one of this project's
 * 60+ other already-verified commands -- more regression surface than
 * this fix needs) via the same real sysctl(2) raw syscall real
 * sysconf.c itself would use (CTL_KERN/KERN_ARGMAX -- confirmed working
 * end-to-end via a standalone QEMU-run diagnostic before wiring this in,
 * not assumed: real sysctl(2) plumbing already exists in
 * libc_gen_syscalls.c for getpagesize()/opendir()'s union-fs check, this
 * is just its first CTL_KERN consumer). Not a hardcoded guess -- the
 * real kernel-reported ARG_MAX, same as every other Darwin process gets.
 *
 * Deliberately a private, self-contained raw `sysctl(2)` call here
 * (same real carry-flag arm64 Darwin syscall convention as every other
 * raw syscall in this project) rather than calling the public sysctl()
 * libc_gen_syscalls.c already provides: this file's own build script
 * (build_libc_stdio.sh) links a standalone smoketest against just
 * libc_stdio.a + libc_locale_for_stdio.a, with no libc_gen.a on that
 * link line at all -- confirmed via a real "undefined _sysctl" link
 * failure when first tried calling the public one, not guessed. Named
 * distinctly from the public `sysctl` symbol so the two coexist without
 * a duplicate-symbol conflict when full simple_cmds.sh binaries (which
 * DO link libc_gen.a) pull in both archives at once. */

#include <stdio.h>    /* FOPEN_MAX */
#include <unistd.h>   /* real _SC_STREAM_MAX/_SC_ARG_MAX values */
#include <sys/sysctl.h>

extern int cerror_nocancel(int);

static int stdio_env_shim_sysctl(int *name, unsigned int namelen, void *oldp,
    size_t *oldlenp, void *newp, size_t newlen) {
    register long x0 asm("x0") = (long)name;
    register long x1 asm("x1") = namelen;
    register long x2 asm("x2") = (long)oldp;
    register long x3 asm("x3") = (long)oldlenp;
    register long x4 asm("x4") = (long)newp;
    register long x5 asm("x5") = (long)newlen;
    register long x16 asm("x16") = 202; /* SYS_sysctl */
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %6, cs"
                 : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4), "+r"(x5), "=r"(is_error)
                 : "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        return cerror_nocancel((int)x0);
    }
    return (int)x0;
}

/* LIBC_ENV_SHIM_NO_SYSCONF (DAR-387): build_libsystem_c.sh rebuilds this
 * object without this stand-in for libsystem_c.dylib, which exports Apple's
 * real third_party/Libc/gen/FreeBSD/sysconf.c instead now that getrlimit()
 * exists (libsystem_kernel). This stand-in answers -1 for _SC_OPEN_MAX,
 * which made Xvfb's xtrans close every listening socket it opened
 * (Xtranssock.c:410-418). The static libc_stdio.a keeps it. */
#ifndef LIBC_ENV_SHIM_NO_SYSCONF
long sysconf(int name) {
    if (name == _SC_STREAM_MAX) {
        return FOPEN_MAX;
    }
    if (name == _SC_ARG_MAX) {
        int mib[2];
        int value;
        size_t len;

        mib[0] = CTL_KERN;
        mib[1] = KERN_ARGMAX;
        len = sizeof(value);
        if (stdio_env_shim_sysctl(mib, 2, &value, &len, ((void *)0), 0) == -1)
            return -1;
        return (long)value;
    }
    return -1;
}
#endif /* LIBC_ENV_SHIM_NO_SYSCONF */

#ifndef LIBC_ENV_SHIM_NO_SYSCONF
int getdtablesize(void) {
    return FOPEN_MAX;
}
#else
/* libsystem_c.dylib's getdtablesize() (DAR-387): the kernel's own answer,
 * syscall #89 (xnu bsd/kern/syscalls.master:142), which returns
 * MIN(RLIMIT_NOFILE, maxfilesperproc) (bsd/kern/kern_descrip.c:564-568).
 * On Apple this is a plain stub in libsystem_kernel; it stays exported here
 * because bash, configd, cron, login and libSystemConfiguration already
 * bind it to libsystem_c. It must agree with the real sysconf(_SC_OPEN_MAX)
 * this dylib now exports, or close-all-fds loops miss descriptors. */
int getdtablesize(void) {
    register long x0 asm("x0") = 0;
    register long x16 asm("x16") = 89; /* SYS_getdtablesize */
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x16)
                 : "memory", "cc", "x1");
    if (is_error) {
        return cerror_nocancel((int)x0);
    }
    return (int)x0;
}
#endif
