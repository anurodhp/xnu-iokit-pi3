/* Real, already-proven raw-syscall fork()/vfork() (DAR-174) -- extracted
 * from cp_mv_syscalls.c (that file's own header explains the real
 * ABI/precedent this follows: raw asm, x16=syscall number, SVC #0x80,
 * carry flag set on error -- same pattern removefile_syscalls.c/
 * copyfile_syscalls.c already use).
 *
 * THE REAL REASON THIS IS ITS OWN FILE rather than just pulling in all
 * of cp_mv_syscalls.c (as build_vipw.sh/build_passwd.sh do): that file
 * ALSO defines `_sigaction`, which chpass's own build needs pulled from
 * liblaunchd_syscalls.a's private_names_compat.o for an unrelated real
 * reason (found via a real `-Wl,-why_load` link trace: chpass_pw_util_
 * freebsd.c's real execl()/execlp() calls need `_execve`/`__execve`
 * from that same archive, and private_names_compat.o happens to bundle
 * `__sigaction` alongside `__execve` in one translation unit) --
 * linking cp_mv_syscalls.c's OWN `_sigaction` definition on top of that
 * is a real duplicate-symbol link error, confirmed by reproducing it.
 * vipw/passwd never hit this because neither one's real object set
 * needs `__execve` from the archive at all (their own `-why_load` trace
 * shows `exec.o` only pulling `_execve` there, satisfied by
 * sys__execve.o alone).
 *
 * fork()/vfork() ARE still needed here for a second, independent real
 * reason: without an object file defining `_fork` directly, ld64
 * resolves chpass_pw_util_freebsd.c's own real fork() calls (pw_mkdb(),
 * pw_edit()) through liblaunchd_syscalls.a's fork_compat.o instead of
 * the real libsystem_c.dylib export (archives are listed before dylibs
 * on this project's link line, so an archive member wins if the symbol
 * is still undefined when the archive is reached) -- and fork_compat.o
 * itself then pulls sys____fork.o (the raw __fork trap), which
 * references a `_current_pid` global this project's build has never
 * defined, producing a real, unrelated `fixup error ... target
 * '__current_pid' does not have address` link failure (confirmed by
 * reproducing it with cp_mv_syscalls.c/this file both absent). Defining
 * `_fork` directly here (object files resolve before archives) avoids
 * that whole chain, same as vipw/passwd's own inclusion of
 * cp_mv_syscalls.c already does for them.
 */

typedef int pid_t;

extern long cerror_nocancel(int err);

#define SYS_fork  2

pid_t fork(void) {
    register long x0 asm("x0") = 0;
    register long x1 asm("x1") = 0;
    register long x16 asm("x16") = SYS_fork;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %2, cs"
                 : "+r"(x0), "+r"(x1), "=r"(is_error)
                 : "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        return (pid_t)cerror_nocancel((int)x0);
    }
    if (x1) {
        /* We are the child -- real fork(2) contract is 0 in the child,
         * regardless of what raw x0 held (real PID of the child, from
         * the kernel's perspective, but POSIX fork() never exposes that
         * to the child itself). */
        return 0;
    }
    return (pid_t)x0;
}

/* Real vfork(2) is a genuine distinct syscall -- this project has no
 * real address-space-sharing/copy-on-write-skip machinery exercised or
 * verified for it, so (same real, already-proven substitution
 * cp_mv_syscalls.c's own mv.c consumer relies on) vfork() is
 * deliberately just fork(): always a valid, if slightly slower,
 * substitute when the child execs or exits immediately after, which is
 * exactly chpass_pw_util_freebsd.c's own real pw_edit()/pw_mkdb() usage
 * (fork() then execl()/execlp(), or _exit()). Not currently called by
 * this build (chpass_pw_util_freebsd.c uses real fork(), not vfork()) --
 * provided anyway for the same EXTRA_FILES-shape consistency
 * cp_mv_syscalls.c's own header already establishes project-wide.
 */
pid_t vfork(void) {
    return fork();
}
