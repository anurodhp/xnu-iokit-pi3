/* apply.c's real Libc/compat-43/sigcompat.c (sigblock/sigsetmask) needs
 * two real pieces this tier has never needed before:
 *
 * - __unix_conforming: a real Apple global, normally set to 1 by crt0
 *   the first time any UNIX03-conforming libc entry point runs (real
 *   sigcompat.c/nanosleep.c both read it to decide legacy-BSD vs
 *   POSIX signal-mask semantics). This project's whole build already
 *   forces `-D__DARWIN_UNIX03=1` unconditionally (every tier, see
 *   build_simple_cmds.sh's own CFLAGS) -- "always UNIX03-conforming" is
 *   the real, correct, permanent answer here, not a placeholder, so a
 *   plain pre-set-to-1 global (rather than replicating crt0's real
 *   lazy first-call-sets-it-to-1 dance) is exact, not approximated.
 *
 * - sigsuspend(2): bsd/kern/syscalls.master #111, NO_SYSCALL_STUB, same
 *   real carry-flag raw-syscall pattern as cp_mv_syscalls.c's other
 *   hand-written entries (sigset_t is a 32-bit value passed directly in
 *   a register per the real syscall prototype, not a pointer).
 */

int __unix_conforming = 1;

extern long cerror_nocancel(int err);

static inline long apply_syscall1(long num, long a0) {
    register long x0 asm("x0") = a0;
    register long x16 asm("x16") = num;
    long is_error;
    asm volatile("svc #0x80\n"
                 "cset %1, cs"
                 : "+r"(x0), "=r"(is_error)
                 : "r"(x16)
                 : "memory", "cc");
    if (is_error) {
        return cerror_nocancel((int)x0);
    }
    return x0;
}

#define SYS_sigsuspend 111

int sigsuspend(unsigned int mask) {
    return (int)apply_syscall1(SYS_sigsuspend, (long)mask);
}

/* pthread_testcancel(): real no-op, same real justification/precedent
 * as ps_err_warn_compat.c's identical definition (this single-thread
 * target has no real pthread cancellation subsystem at all,
 * bsd_pthread_stub.c) -- duplicated here in its own small file rather
 * than reusing that one directly, since it also (redundantly) defines
 * err()/warn()/flsl(), which would collide with this tier's own shared
 * common_err.o and, for chown/etc, libremovefile.a's own flsl() --
 * confirmed as a real conflict, not guessed preemptively. */
void pthread_testcancel(void) {
}
