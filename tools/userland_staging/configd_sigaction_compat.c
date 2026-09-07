/* Real gap: third_party/Libc/gen/FreeBSD/signal.c's real, unmodified
 * signal() implementation calls a C-level function named `_sigaction`
 * (one leading underscore) internally, which no library in this project
 * defines. A narrow, honest, one-line forwarding shim, same class as
 * this project's other small naming-mismatch fixes (sc_syscall_compat.c
 * etc), not a fabricated sigaction() reimplementation.
 *
 * ---- What it must forward TO (DAR-262, fixed 2026-09-07) ----
 *
 * This file used to forward to the RAW KERNEL TRAP, `__sigaction` (two
 * leading underscores, build_libsystem_kernel.sh's sys___sigaction.o).
 * That was wrong, and silently so: the raw trap does not take a
 * `struct sigaction` at all.
 *
 * The trap's real kernel side (third_party/xnu-7195/bsd/kern/kern_sig.c,
 * `sigaction()` at :447) copies in a `struct __user64_sigaction`, which
 * is `struct __sigaction` (bsd/sys/signal.h:376) -- and that struct has
 * an extra `sa_tramp` member wedged between the handler and the mask:
 *
 *     struct __sigaction {                 struct sigaction {
 *         union __sigaction_u ...;   0         union __sigaction_u ...;   0
 *         void (*sa_tramp)(...);     8         sigset_t sa_mask;          8
 *         sigset_t sa_mask;         16         int      sa_flags;        12
 *         int      sa_flags;        20     };  // 16 bytes
 *     };  // 24 bytes
 *
 * So handing the kernel a 16-byte `struct sigaction` made it read 24
 * bytes and land every field but the handler on the wrong data:
 * `sa_tramp` came from the caller's `{sa_mask, sa_flags}` words, and
 * `sa_mask`/`sa_flags` came from whatever followed the struct on the
 * caller's stack. XNU's sendsig() enters userspace AT sa_tramp on
 * delivery, so any real handler installed through here was a wild jump
 * waiting for its signal, and SA_VALIDATE_SIGRETURN_FROM_SIGTRAMP was
 * never set either.
 *
 * Real Apple does not have a distinct `_sigaction` function at all --
 * third_party/Libc/fbsdcompat/_fbsd_compat_.h:129 is literally
 * `#define _sigaction sigaction`. The real chain is
 * sigaction() (third_party/Libc/sys/sigaction.c:43)
 *   -> __platform_sigaction() (third_party/libplatform/src/setjmp/
 *      generic/sigaction.c:47)   <- the layer that builds the RIGHT
 *      struct: sets sa_tramp = _sigtramp and ORs in
 *      SA_VALIDATE_SIGRETURN_FROM_SIGTRAMP
 *   -> __sigaction() raw trap
 *
 * Both of those real files are already compiled into this project's
 * libsystem_c.dylib (build_libsystem_c.sh:146-162, Libc_sys_sigaction.o
 * and libplatform_sigaction.o), and `_sigtramp` is already a real export
 * of libsystem_kernel.dylib -- so the correct implementation was present
 * all along and simply was not being reached from here.
 *
 * Forwarding to the public `sigaction()` is therefore both the minimal
 * fix and the exact Apple-parity one. It also matches what this project
 * ALREADY does for the same symbol everywhere else:
 * launchd_syscall_compat_headers/private_names_compat.c:32 has carried
 * this identical forwarder all along, which is why 79 other shipped
 * binaries were unaffected -- only configd.macho and notifyd.macho, the
 * two images built against THIS file, ever had the broken variant.
 *
 * configd is a real victim, not a theoretical one: configd.m:421 does
 * `nact.sa_handler = catcher; ... sigaction(SIGHUP, &nact, NULL)` --
 * a genuine handler, installed through this path.
 */
#include <sys/signal.h>

/* The real, public sigaction(3) -- libsystem_c.dylib's, via real Libc
 * sys/sigaction.c + libplatform's __platform_sigaction(). Declared here
 * rather than relying on <sys/signal.h>'s own declaration only so the
 * forwarding target is unambiguous to a reader. */
extern int sigaction(int, const struct sigaction * __restrict,
                     struct sigaction * __restrict);

int
_sigaction(int sig, const struct sigaction *act, struct sigaction *oact)
{
	return sigaction(sig, act, oact);
}
