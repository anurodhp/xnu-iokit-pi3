#ifndef _CONFIGD_SIGACTION_COMPAT_H_
#define _CONFIGD_SIGACTION_COMPAT_H_
/* Real gap: this pinned tag's Libc/include/signal.h never declares the
 * internal `_sigaction()` entry point that Libc/gen/FreeBSD/signal.c's
 * own real, unmodified signal() implementation calls -- see
 * configd_sigaction_compat.c's own header comment for the real definition
 * (a one-line forward to the real `__sigaction` raw trap). -include'd
 * globally (harmless -- just a prototype) so signal.c's own real call
 * site resolves without an implicit-declaration error.
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
