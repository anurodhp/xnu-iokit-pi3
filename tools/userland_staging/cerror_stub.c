/* Minimal stand-in for libsyscall/custom/errno.c's cerror()/cerror_nocancel()
 * (real Apple source, third_party/xnu-7195/libsyscall/custom/errno.c) - same
 * pattern as this project's kernel-side stand-ins (bsd_pthread_stub.c for
 * pthread.kext, the from-scratch SHA2 impl for corecrypto) for a real Apple
 * subsystem this freestanding target doesn't have yet.
 *
 * Apple's real cerror_nocancel() stores errno via _os_tsd_get_direct(),
 * which on arm64 reads the TPIDRRO_EL0 register and indexes into whatever
 * per-thread TSD block is installed there (libsyscall/os/tsd.h). That block
 * is set up by libpthread's bsdthread_create()/thread bootstrap - which
 * this target never calls (tools/init_binary/init.c and this smoketest
 * both run with no pthread library at all). Using the real errno.c as-is
 * would read TPIDRRO_EL0's base and dereference it - on a process with no
 * pthread TSD block installed that register's contents are undefined, so
 * this is a real (not hypothetical) NULL/garbage-pointer-deref risk, not
 * just unnecessary complexity to route around.
 *
 * This stub instead uses a single plain global `errno`, matching this
 * project's current single-threaded-only reality (tools/init_binary/init.c
 * has exactly one thread of control per process too). Real per-thread errno
 * needs a real TSD/pthread bootstrap - out of scope until libpthread itself
 * is being built, tracked as follow-on Phase 4 work.
 */

int errno;

int *__error(void) {
	return &errno;
}

typedef unsigned __int128 cerror_return_t;

/* Apple's real cerror() calls this first; libpthread cancellation doesn't
 * exist on this target, so it's a deliberate no-op, not a missing feature. */
void _pthread_exit_if_canceled(int error) {
	(void)error;
}

__attribute__((noinline))
cerror_return_t cerror_nocancel(int err) {
	errno = err;
	return -1;
}

__attribute__((noinline))
cerror_return_t cerror(int err) {
	_pthread_exit_if_canceled(err);
	return cerror_nocancel(err);
}
