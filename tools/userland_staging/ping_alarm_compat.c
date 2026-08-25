/* alarm(2): no real BSD syscall exists -- real Apple implements it via
 * setitimer(2) + SIGALRM delivery, the same real interval-timer
 * subsystem bash_syscalls.c's own alarm() stub already documents this
 * project has never wired up end-to-end (see that file's header
 * comment). Same honest, already-established stub reused here rather
 * than reinvented: always reports "no previous alarm pending" (0) and
 * does NOT schedule a real SIGALRM.
 *
 * Real, documented behavioral gap, not fabricated support: ping.c's own
 * only two real call sites are (1) main()'s `if (alarmtimeout > 0)
 * alarm((unsigned)alarmtimeout)` -- the `-t timeout` flag, not exercised
 * by a default/no-flags invocation -- and (2) the matching `sigaction
 * SIGALRM` install, itself already gated behind the identical
 * `alarmtimeout > 0` check, so SIGALRM is never even armed as a signal
 * ping.c expects to receive unless `-t` is passed. A default ping run
 * (no `-t`) never calls this function at all; it only needs to be
 * linkable.
 */
unsigned int alarm(unsigned int seconds) {
    (void)seconds;
    return 0;
}
