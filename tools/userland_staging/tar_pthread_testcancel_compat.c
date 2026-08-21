/* pthread_testcancel(3): real termios.c's tcdrain() (pulled in whole-.o
 * from libc_gen.a by tar(1)'s own real terminal-related calls) calls it
 * as a real POSIX cancellation checkpoint. This project's libpthread.a
 * build doesn't include real Apple's cancellation-support file (out of
 * scope for the single-thread-correct primitives tier, see
 * build_libpthread.sh) -- and there IS no real thread-cancellation
 * subsystem on this target regardless (no real pthread_create). A
 * no-op here is the honest, correct answer: "no cancellation request is
 * ever pending" is unconditionally true when no mechanism to request
 * one exists, not a fabricated success. Same real gap, same real fix,
 * already independently established for apply(1)/bash(1)/ps(1) (see
 * apply_sigsuspend_compat.c/bash_syscalls.c/ps_err_warn_compat.c's own
 * identical copies) -- duplicated here rather than shared since none of
 * those three are linked into tar.macho.
 */
void
pthread_testcancel(void)
{
}
