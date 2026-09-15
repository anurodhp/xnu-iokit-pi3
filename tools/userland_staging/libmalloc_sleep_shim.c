/* Real minimal sleep() body for the one rare call site real Apple
 * third_party/libmalloc/src/malloc_printf.c has (malloc_report()'s
 * "give a debugger time to attach" pause before a fatal error report --
 * gated behind malloc debug env vars, essentially never hit in normal
 * operation). Apple's real Libc sleep() is FreeBSD-sourced
 * (third_party/Libc/gen/FreeBSD/sleep.c) with full EINTR-retry semantics
 * on top of nanosleep-equivalent primitives -- deliberately NOT pulled in
 * here (a bigger dependency chain than this rare debug path warrants).
 * WARNING (corrected 2026-09-15, DAR-309): this call DOES NOT ACTUALLY
 * WAIT, and the claim this comment used to make -- that cond_sem=
 * mutex_sem=0 means "just wait for the timeout" -- is FALSE. Real
 * kernel: with mutex_sem==0 and timeout==1, __semwait_signal_nocancel()
 * (xnu-7195 bsd/kern/kern_sig.c:1232) calls
 * semaphore_timedwait_trap_internal(cond_sem, ...), which calls
 * port_name_to_semaphore() (osfmk/kern/sync_sema.c:945); that returns
 * KERN_INVALID_NAME immediately for an invalid port name
 * (osfmk/kern/ipc_sync.c:63, !MACH_PORT_VALID(0)) WITHOUT ever reaching
 * semaphore_wait_internal(). Real Apple passes a REAL semaphore here
 * (third_party/Libc/gen/nanosleep.c:104 -- clock_sem as cond_sem,
 * MACH_PORT_NULL only as mutex_sem).
 *
 * So this returns instantly and malloc_report()'s "give a debugger time
 * to attach" pause never happens. Left as-is deliberately for now, NOT
 * because it is correct: wiring mach_ipc_clock_init() in here would put
 * a Mach-IPC/pthread-TSD bootstrap inside malloc's own fatal-error
 * path, a real re-entrancy hazard (that bootstrap can allocate), for a
 * debug-only pause that is essentially never hit. Tracked separately --
 * do NOT copy this cond_sem=0 pattern anywhere else; it is exactly what
 * caused DAR-309's ~100% PID-1 CPU when a caller wrapped it in a retry
 * loop. Use simple_cmds_nanosleep_compat.c's real-semaphore form. */
extern int __semwait_signal(int cond_sem, int mutex_sem, int timeout,
    int relative, long long tv_sec, int tv_nsec);

unsigned int _mal_semwait_sleep(unsigned int seconds) {
    __semwait_signal(0, 0, 1, 1, (long long)seconds, 0);
    return 0;
}
