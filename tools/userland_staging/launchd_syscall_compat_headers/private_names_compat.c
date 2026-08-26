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
 * (execve: create-syscalls.pl raw trap; write: same; sigaction: real
 * FreeBSD/signal.c's own private-name callee, wired to the public
 * sigaction() this project's create-syscalls.pl-generated raw trap
 * provides) are what gen/FreeBSD/exec.c and gen/FreeBSD/signal.c
 * actually call.
 */
#include <sys/types.h>
#include <signal.h>

extern int execve(const char *, char *const *, char *const *);
int _execve(const char *path, char *const *argv, char *const *envp) {
	return execve(path, argv, envp);
}

extern ssize_t write(int, const void *, size_t);
ssize_t _write(int fd, const void *buf, size_t nbyte) {
	return write(fd, buf, nbyte);
}

extern int sigaction(int, const struct sigaction *, struct sigaction *);
int _sigaction(int sig, const struct sigaction *act, struct sigaction *oact) {
	return sigaction(sig, act, oact);
}
int _sigaction_nobind(int sig, const struct sigaction *act, struct sigaction *oact) {
	return sigaction(sig, act, oact);
}
