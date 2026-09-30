/* vfork() only (DAR-174, reduced by DAR-292). This file used to also carry a
 * private raw-svc fork(); that copy is gone: fork() is now owned by
 * libsystem_c.dylib (libsystem_c_fork_compat.c: real atfork handlers +
 * _libc_fork_child(); raw __fork trap in libsystem_kernel), and consumers
 * (chpass, pw, su, sudo) link liblaunchd_syscalls_no_fork.a
 * (make_no_fork_archive.sh) so no archive member (fork_compat.o, exec.o,
 * wait4_compat.o, ...) can satisfy those symbols ahead of the dylib.
 * vfork stays local because no shipped dylib exports _vfork.
 */

typedef int pid_t;
extern pid_t fork(void);

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
