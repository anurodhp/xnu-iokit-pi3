/* Minimal stand-in for libsyscall/custom/errno.c's cerror()/cerror_nocancel()
 * (real Apple source, third_party/xnu-7195/libsyscall/custom/errno.c) - same
 * pattern as this project's kernel-side stand-ins (bsd_pthread_stub.c for
 * pthread.kext, the from-scratch SHA2 impl for corecrypto) for a real Apple
 * subsystem this freestanding target doesn't have yet.
 *
 * Apple's real cerror_nocancel() stores errno via _os_tsd_get_direct(),
 * which on arm64 reads the TPIDRRO_EL0 register and indexes into whatever
 * per-thread TSD block is installed there (libsyscall/os/tsd.h). That block
 * is set up by libpthread's thread bootstrap. This stand-in began as a single
 * plain global `errno` because no pthread library existed here yet; libpthread
 * has since arrived (DAR-416) and every thread has its own errno cell in TSD
 * slot 1, so __error() now returns that when it is set and falls back to the
 * global only when TPIDRRO_EL0 / the slot is empty (code running before
 * libpthread's init, images without a pthread TSD block). The register is
 * checked for zero first, which Apple's version does not do, because this port
 * still has such processes.
 */

/* errno is per thread, as in real Apple's libsyscall/custom/errno.c (third_party/xnu-7195/libsyscall/custom/errno.c):
 * libpthread installs, for every thread, a pointer to that thread's own int in TSD slot 1 (__TSD_ERRNO,
 * libsyscall/os/tsd.h:37; libpthread private/tsd_private.h:63 _PTHREAD_TSD_SLOT_ERRNO), and __error() returns it. The plain
 * global below is only the fallback for code that runs before libpthread has set the slot up (dyld's early start, static
 * images without a pthread TSD block).
 *
 * This file used to return the global unconditionally ("single-threaded-only reality", see the history above). That broke
 * the first code that reads errno the way libpthread itself does, through the TSD slot: a syscall wrapper (libsystem_kernel's
 * __psynch_cvwait, via cerror_nocancel) stored the error in the global, libpthread's pthread_cond_timedwait read the slot,
 * saw a stale value and turned a plain ETIMEDOUT into EINVAL (DAR: timed cond waits, GLib aborts "Invalid argument").
 * tests/pi/errno_slot.c in the florence repo shows the mismatch; it also made errno shared by all threads. */
int errno;

static inline int *tsd_errno_slot(void) {
	unsigned long tsd;
	__asm__("mrs %0, TPIDRRO_EL0" : "=r"(tsd));
	tsd &= ~7ul;     /* the low bits of TPIDRRO_EL0 carry flags (libsyscall/os/tsd.h _os_tsd_get_base) */
	if (tsd == 0) {
		return 0;
	}
	return (int *)((void **)tsd)[1];     /* __TSD_ERRNO */
}

int *__error(void) {
	int *p = tsd_errno_slot();
	return p != 0 ? p : &errno;
}

typedef unsigned __int128 cerror_return_t;

/* Apple's real cerror() calls this first. libsystem_kernel builds this file
 * with CERROR_STUB_LIBKERNEL_FUNCPTR (DAR-416): there it is the real
 * _libc_funcptr.c thunk into libsystem_pthread's pthread_cancelable.c,
 * registered through __libkernel_init. The static images that still link
 * this file keep the no-op. */
#ifdef CERROR_STUB_LIBKERNEL_FUNCPTR
extern void _pthread_exit_if_canceled(int error);
#else
void _pthread_exit_if_canceled(int error) {
	(void)error;
}
#endif

__attribute__((noinline))
cerror_return_t cerror_nocancel(int err) {
	errno = err;                      /* the fallback, as in Apple's errno.c, and */
	int *p = tsd_errno_slot();
	if (p != 0) {
		*p = err;                     /* this thread's errno */
	}
	return -1;
}

__attribute__((noinline))
cerror_return_t cerror(int err) {
	_pthread_exit_if_canceled(err);
	return cerror_nocancel(err);
}
