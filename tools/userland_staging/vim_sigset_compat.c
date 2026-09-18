/* Real sigset(3) for vim's os_unixx.h (DAR-185):
 *
 *     #if defined(HAVE_SIGSET) && !defined(signal)
 *     # define signal sigset
 *     #endif
 *
 * Apple's real config.h defines HAVE_SIGSET, so every real signal(2)
 * call site in os_unix.c textually becomes a sigset(3) call -- a real,
 * standard SysV/BSD "reliable signals" interface Apple's own libc has
 * shipped for decades, but this project's pinned third_party/Libc
 * checkout does not include (confirmed via find: no sigset.c anywhere
 * in the pinned tree, unlike its sibling sigsetops.c which is a
 * genuinely different real function group -- sigemptyset/sigfillset/
 * sigaddset/sigdelset/sigismember).
 *
 * sigset(3)'s real, standardized behavior (X/Open XSH5, matching every
 * real BSD/glibc/Darwin implementation of it): install a handler and
 * return the previous one, exactly like signal(2), EXCEPT the signal
 * being handled is automatically added to the handler's own blocked
 * mask for its duration (so it cannot recursively interrupt itself) --
 * i.e. the one real behavioral difference from a bare signal(2) call
 * with SA_RESTART and an empty sa_mask is that self-blocking. Expressed
 * directly in terms of real sigaction(2), which this project already
 * has (libsystem_kernel.dylib).
 */
#include <signal.h>

typedef void (*vim_sig_t)(int);

vim_sig_t
sigset(int sig, vim_sig_t disp)
{
	struct sigaction sa, osa;

	sa.sa_handler = disp;
	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, sig);
	sa.sa_flags = 0;
	if (sigaction(sig, &sa, &osa) == -1)
		return (vim_sig_t)-1;
	return osa.sa_handler;
}
