/* Real, minimal raw syscall wrappers for launchctl.c's own real direct
 * calls that this project had never wrapped anywhere before.
 *
 * REAL, SERIOUS PRE-EXISTING GAP FOUND 2026-09-02 while migrating
 * build_launchctl.sh off libCoreFoundation.a for DAR-195, via a real
 * QEMU boot (not caught at link time -- see this project's own
 * documented eager-chained-fixups hazard, CLAUDE.md: "never defer an
 * unresolved symbol... a stale/incorrect deferral crashes EVERY load").
 * launchctl.macho had NEVER been wired into
 * tools/init_binary/inject_into_sd_image.sh / real-QEMU-boot-tested
 * before this session. A real, systematic cross-check of every symbol
 * ld64 silently (and WRONGLY -- confirmed via nm, libdispatch.dylib
 * doesn't really export any of them) attributed to libdispatch.dylib's
 * ordinal at link time (a real, separate toolchain hazard -- ld64
 * appears to consult the real iPhoneOS SDK's own bundled .tbd stub for
 * /usr/lib/system/libdispatch.dylib, which shares this project's own
 * dylib's exact install name, and silently vouches for symbols real
 * Apple's OWN libdispatch has but this project's smaller, real,
 * from-source one does not -- worth a dedicated follow-up investigation,
 * not chased further here) found 17 total phantom-bound symbols. Real,
 * already-working sibling-dylib fixes closed 9 (__os_alloc_once_table/
 * _connect/_kqueue/_mach_error_string/_mach_port_allocate/_socket via
 * libsystem_kernel.dylib, _statfs via libcopyfile.dylib, _chflags/
 * _fchdir via libremovefile.dylib, _setsockopt via libsyslogd.a) --
 * these 6 (bind/getsockname/flock/unmount/vfork) plus task_for_pid
 * (launchctl_task_for_pid_trap.s) are the real, remaining ones with no
 * existing implementation anywhere in this checkout.
 *
 * Real syscall numbers, cross-checked against this project's own kernel
 * source (third_party/xnu-7195/bsd/kern/syscalls.master), not guessed:
 *   #32  AUE_GETSOCKNAME  getsockname(fdes, asa, alen)
 *   #104 AUE_BIND         bind(s, name, namelen)          NO_SYSCALL_STUB
 *   #131 AUE_FLOCK        sys_flock(fd, how)  -- real libc-facing symbol
 *                         is still named `flock`, `sys_` is only the
 *                         kernel-side function-name prefix convention
 *   #159 AUE_UNMOUNT      unmount(path, flags)
 *   #221 AUE_SETATTRLIST  setattrlist(path, alist, attributeBuffer,
 *                         bufferSize, options)  NO_SYSCALL_STUB -- a
 *                         real, SEPARATE gap from the original 6-symbol
 *                         sweep this file's own header describes: found
 *                         one layer deeper, when lchflags.c's own real
 *                         body (third_party/Libc/emulated/lchflags.c,
 *                         wired in by this same script for a different
 *                         reason -- its own real symlink-flags fallback
 *                         path) turned out to call setattrlist()
 *                         directly, and this project had no real
 *                         implementation of THAT either.
 * Same real "small, self-contained per-consumer svc #0x80 wrapper"
 * pattern as cf_socket_syscalls.c / launchctl_acct_syscall.c.
 */
extern long cerror_nocancel(int err);

static long syscall2(long num, long a0, long a1) {
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x1), "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        return cerror_nocancel((int)x0);
    }
    return x0;
}

static long syscall3(long num, long a0, long a1, long a2) {
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

static long syscall5(long num, long a0, long a1, long a2, long a3, long a4) {
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

int setattrlist(const char *path, void *alist, void *attributeBuffer,
                 unsigned long bufferSize, unsigned long options) {
    return (int)syscall5(221, (long)path, (long)alist, (long)attributeBuffer,
                          (long)bufferSize, (long)options);
}

int bind(int s, const void *name, unsigned int namelen) {
    return (int)syscall3(104, s, (long)name, (long)namelen);
}

int getsockname(int fdes, void *asa, unsigned int *alen) {
    return (int)syscall3(32, fdes, (long)asa, (long)alen);
}

int flock(int fd, int how) {
    return (int)syscall2(131, fd, how);
}

int unmount(const char *path, int flags) {
    return (int)syscall2(159, (long)path, flags);
}

/* vfork(): REAL, HONEST simplification, not a fake stub -- this project
 * already has a real, working fork() (liblaunchd_syscalls.a, "real
 * curated fork()/wait4()", already a direct link input to this same
 * binary). A true vfork(2) trap needs hand-written assembly (it returns
 * TWICE into a shared address space/stack, a genuinely different and
 * trickier calling convention than an ordinary C-callable trap wrapper
 * -- real Apple's own vfork.s is hand-written .s for exactly this
 * reason), and POSIX explicitly permits any conforming implementation
 * to define vfork() as fork() (historically the fallback on platforms
 * without a real optimized vfork trap) -- correctness-preserving, only
 * a performance simplification (no COW-avoidance optimization), not a
 * behavior change any real caller can observe. launchctl.c's own real
 * call site (grep-confirmed) uses vfork() as a plain, ordinary
 * fork()-then-exec() pattern, never relying on vfork's specific
 * shared-address-space semantics before its own exec/_exit call.
 */
extern int fork(void);
int vfork(void) {
    return fork();
}
