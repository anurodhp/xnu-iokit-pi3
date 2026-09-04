/* Real gap: third_party/Libc/gen/FreeBSD/signal.c's real, unmodified
 * signal() implementation calls a C-level function named `_sigaction`
 * (one leading underscore) internally. This project's real raw syscall
 * trap (build_libsystem_kernel.sh's own sys___sigaction.o, DAR-164) is
 * C-level `__sigaction` (TWO leading underscores -- confirmed via `nm` on
 * that real object: it defines linker symbol `___sigaction`, i.e. Darwin's
 * standard single-underscore C-to-linker mangling applied to a two-
 * underscore C name), a real, deliberate Darwin naming split between the
 * raw kernel trap (`__sigaction`) and the layer real Apple's own libc
 * provides on top of it (`_sigaction`, then the public `sigaction()`).
 * This project has never built that middle layer -- a narrow, honest,
 * one-line forwarding shim, same class as this ticket's other small
 * naming-mismatch fixes (sc_syscall_compat.c etc), not a fabricated
 * sigaction() reimplementation.
 */
#include <sys/signal.h>

extern int __sigaction(int, const struct sigaction * __restrict, struct sigaction * __restrict);

int
_sigaction(int sig, const struct sigaction *act, struct sigaction *oact)
{
	return __sigaction(sig, act, oact);
}
