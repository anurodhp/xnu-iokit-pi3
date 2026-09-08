#ifndef _CONFIGD_SIGACTION_COMPAT_H_
#define _CONFIGD_SIGACTION_COMPAT_H_
/* Real gap: this pinned tag's Libc/include/signal.h never declares the
 * internal `_sigaction()` entry point that Libc/gen/FreeBSD/signal.c's
 * own real, unmodified signal() implementation calls -- see
 * configd_sigaction_compat.c's own header comment for the real
 * definition. -include'd globally (harmless -- just a prototype) so
 * signal.c's own real call site resolves without an implicit-
 * declaration error.
 *
 * Corrected 2026-09-08 (code review): this comment used to describe
 * _sigaction() as a one-line forward to the raw `__sigaction` kernel
 * trap. That was DAR-262's actual bug, not its fix -- forwarding to the
 * raw trap hands the kernel a 16-byte struct sigaction where it expects
 * a 24-byte struct __sigaction (sa_tramp between the handler and the
 * mask), silently corrupting every real signal handler installed
 * through this path. The real, current definition (see the .c file)
 * forwards to the public sigaction() instead -- do not "fix" it back to
 * match this header's old wording.
 */
#include <sys/signal.h>
#ifdef __cplusplus
extern "C" {
#endif
int _sigaction(int, const struct sigaction * __restrict, struct sigaction * __restrict);
#ifdef __cplusplus
}
#endif
#endif /* _CONFIGD_SIGACTION_COMPAT_H_ */
