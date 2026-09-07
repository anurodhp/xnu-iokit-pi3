/* DELIBERATELY BROKEN copy of the pre-DAR-262 _sigaction shim, kept so
 * the failing case is reproducible from a checked-in script rather than
 * only existing in a git history or an agent transcript (CLAUDE.md:
 * "every built artifact needs a reproducing script").
 *
 * This is the EXACT body configd_sigaction_compat.c carried before
 * DAR-262 was fixed: it hands a 16-byte `struct sigaction` straight to
 * the raw `__sigaction` kernel trap, which copies in a 24-byte
 * `struct __user64_sigaction`. See configd_sigaction_compat.c's own
 * header for the full struct-layout write-up.
 *
 * Used ONLY by build_sigaction_tramp_smoketest.sh to produce
 * sigaction_tramp_smoketest_broken.macho, the "before" half of that
 * test's real before/after evidence. NOTHING SHIPPED LINKS THIS. It is
 * not injected into the SD image by
 * tools/init_binary/inject_into_sd_image.sh's normal path -- the broken
 * binary is copied under its own distinct name so it can never be
 * mistaken for the real one.
 */
#include <sys/signal.h>

extern int __sigaction(int, const struct sigaction * __restrict, struct sigaction * __restrict);

int
_sigaction(int sig, const struct sigaction *act, struct sigaction *oact)
{
	return __sigaction(sig, act, oact);
}
