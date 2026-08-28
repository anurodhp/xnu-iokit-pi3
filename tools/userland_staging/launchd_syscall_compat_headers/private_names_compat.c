/* Real Apple libsyscall convention: several public wrapper names also
 * get an internal, single-underscore-prefixed "private" alias, so
 * other pieces of Libc can call the real syscall without going through
 * the public symbol (which an application could interpose). Real
 * Apple's own build generates these via a linker alias/export-map step
 * this project doesn't have; third_party/Libc/fbsdcompat/sys/cdefs.h's
 * own `__weak_reference(sym,alias)` macro (empty here, no real linker
 * weak-symbol-aliasing support in this minimal environment either) is
 * the same real gap already handled the same way. These three thin
 * forwards to the already-real public functions this project generates
 * (execve: create-syscalls.pl raw trap; sigaction: real FreeBSD/
 * signal.c's own private-name callee, wired to the public sigaction()
 * this project's create-syscalls.pl-generated raw trap provides) are
 * what gen/FreeBSD/exec.c and gen/FreeBSD/signal.c actually call.
 * `_write` used to be forwarded here too, but this launchd tier also
 * links `libc_locale_for_stdio.a`, whose own `libc_locale_syscalls.c`
 * already provides a real, independent `_write` raw-syscall
 * implementation -- a genuine real trial link found both, as a hard
 * duplicate-symbol conflict, not just a theoretical risk. Dropped here
 * in favor of that one, rather than the reverse, since
 * libc_locale_for_stdio.a is the lower-level, more broadly-shared tier.
 */
#include <sys/types.h>
#include <signal.h>

extern int execve(const char *, char *const *, char *const *);
int _execve(const char *path, char *const *argv, char *const *envp) {
	return execve(path, argv, envp);
}

extern int sigaction(int, const struct sigaction *, struct sigaction *);
int _sigaction(int sig, const struct sigaction *act, struct sigaction *oact) {
	return sigaction(sig, act, oact);
}
/* _sigaction_nobind used to be forwarded here too -- real
 * third_party/Libc/sys/sigaction.c (pulled in via launchd_libc_extras.a's
 * own Libc_sys_sigaction.o) already defines it for real; same duplicate-
 * symbol class as `_write` above, dropped in favor of the real Apple
 * source. */
