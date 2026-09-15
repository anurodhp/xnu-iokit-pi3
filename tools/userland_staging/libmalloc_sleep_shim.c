/* Real minimal sleep() body for the one rare call site real Apple
 * third_party/libmalloc/src/malloc_printf.c has (malloc_report()'s
 * "give a debugger time to attach" pause at that file's line 177 --
 * gated behind `flags & (MALLOC_REPORT_DEBUG | MALLOC_REPORT_CRASH)`
 * plus a nonzero malloc debug env var, and immediately followed by
 * abort() on the CRASH path, so essentially never hit in normal
 * operation). Apple's real Libc sleep() is FreeBSD-sourced
 * (third_party/Libc/gen/FreeBSD/sleep.c) with full EINTR-retry semantics
 * on top of nanosleep-equivalent primitives -- deliberately NOT pulled in
 * here (a bigger dependency chain than this rare debug path warrants).
 *
 * FIXED 2026-09-15 (DAR-312, same root cause as DAR-309). This used to
 * call __semwait_signal(0, 0, 1, 1, seconds, 0), justified by a comment
 * claiming that cond_sem=mutex_sem=0 meant "just wait for the timeout",
 * with "no semaphore" involved. That claim was FALSE and this function
 * therefore never waited at all -- it returned instantly. Real chain:
 * __semwait_signal_nocancel() (xnu-7195 bsd/kern/kern_sig.c:1232) with
 * mutex_sem==0 and timeout==1 calls semaphore_timedwait_trap_internal()
 * (osfmk/kern/sync_sema.c:945), which calls port_name_to_semaphore(),
 * which returns KERN_INVALID_NAME immediately for !MACH_PORT_VALID(0)
 * (osfmk/kern/ipc_sync.c:63) WITHOUT ever reaching
 * semaphore_wait_internal(). That false claim originated in THIS file
 * and then propagated by citation into ping_extra_syscalls.c and
 * launchd_audit_syscalls_compat.c -- the latter is what caused DAR-309's
 * ~100% PID-1 CPU once a retry loop got wrapped around it.
 *
 * Why this does NOT use real Apple's fix (a real semaphore as cond_sem,
 * third_party/Libc/gen/nanosleep.c:104 via mach_ipc_clock_init() +
 * g_mach_ipc_clock_sem, which is what ping and launchd now use): that
 * needs semaphore_create()/host_get_clock_service(), which live ONLY in
 * libmach_ipc.a. 36 build_*.sh scripts alias sleep to this symbol and 22
 * of them do not link libmach_ipc.a -- including the deliberately fully
 * static binaries (build_bash.sh/build_login.sh/build_getty.sh, see
 * CLAUDE.md). Taking that dependency here would break all 22 links for
 * the sake of a debug-only pause.
 *
 * Real dependency-free alternative that genuinely blocks: poll(2) with
 * nfds=0 (real BSD syscall 230, bsd/kern/syscalls.master:333). XNU
 * explicitly supports and documents this exact use -- bsd/kern/
 * sys_generic.c's own comment above the nfds check reads "If user space
 * passed 0 FDs, then respect any timeout value passed. This is an
 * extremely inefficient sleep." (inefficient because it allocates a
 * kqueue to do it -- irrelevant for a once-ever debug pause, and
 * correctness beats efficiency here since the alternative was not
 * sleeping at all).
 *
 * Deliberately keeps the original's no-EINTR-retry behavior: that part
 * of the old comment WAS legitimate -- malloc_report()'s pause genuinely
 * doesn't need signal-interrupt correctness, unlike launchd's
 * update_thread() (DAR-309), which does and now has it. */

/* poll(2) -- real BSD syscall 230. Raw svc, same real convention as
 * every other raw syscall stub in this project. Return value is
 * deliberately ignored (this is a best-effort debug pause); no
 * cerror_nocancel() call, so this adds no link dependency at all. */
static inline long mal_sleep_poll(long fds, long nfds, long timeout_ms) {
    register long x0 asm("x0") = fds;
    register long x1 asm("x1") = nfds;
    register long x2 asm("x2") = timeout_ms;
    register long x16 asm("x16") = 230;
    asm volatile("svc #0x80"
                 : "+r"(x0)
                 : "r"(x1), "r"(x2), "r"(x16)
                 : "memory", "cc");
    return x0;
}

unsigned int _mal_semwait_sleep(unsigned int seconds) {
    /* Clamp so the seconds->milliseconds conversion cannot overflow
     * poll()'s int timeout. Real callers pass a small debug value. */
    if (seconds > 1000000U) {
        seconds = 1000000U;
    }
    mal_sleep_poll(0, 0, (long)seconds * 1000);
    return 0;
}
