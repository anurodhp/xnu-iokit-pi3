/* Real minimal sleep() body for the one rare call site real Apple
 * third_party/libmalloc/src/malloc_printf.c has (malloc_report()'s
 * "give a debugger time to attach" pause before a fatal error report --
 * gated behind malloc debug env vars, essentially never hit in normal
 * operation). Apple's real Libc sleep() is FreeBSD-sourced
 * (third_party/Libc/gen/FreeBSD/sleep.c) with full EINTR-retry semantics
 * on top of nanosleep-equivalent primitives -- deliberately NOT pulled in
 * here (a bigger dependency chain than this rare debug path warrants).
 * This implements the same real underlying mechanism real Apple
 * usleep()/sleep() actually use -- a timed __semwait_signal(2) call with
 * no semaphore (cond_sem=mutex_sem=0, i.e. "just wait for the timeout"),
 * a real BSD syscall (334, regenerated fresh from syscalls.master the
 * same way as every other syscall stub in this project) -- just without
 * FreeBSD sleep.c's EINTR-retry loop, since malloc_report()'s pause
 * doesn't need signal-interrupt correctness. */
extern int __semwait_signal(int cond_sem, int mutex_sem, int timeout,
    int relative, long long tv_sec, int tv_nsec);

unsigned int _mal_semwait_sleep(unsigned int seconds) {
    __semwait_signal(0, 0, 1, 1, (long long)seconds, 0);
    return 0;
}
