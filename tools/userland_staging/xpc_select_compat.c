/* Real, curated select() -- verbatim non-legacy/non-cancelable body from
 * third_party/xnu-7195/libsyscall/wrappers/select-base.c (real Apple
 * source, the "1050 variant" -- the modern, non-legacy default path).
 * Curated down to just select() rather than compiling the whole file:
 * that file also defines pselect()/a pselect_emulated() fallback this
 * project has no real caller for yet, which pulls in
 * __pthread_sigmask/__pselect_nocancel -- deferred, not this XPC pass's
 * concern (confirmed via grep: no real pselect() call site anywhere in
 * third_party/PureDarwin's actual launchd/libxpc source).
 */
#include <sys/select.h>
#include <sys/time.h>

extern int __select_nocancel(int, fd_set * __restrict, fd_set * __restrict,
    fd_set * __restrict, struct timeval * __restrict);
extern int *__error(void);
#define errno (*__error())
#define EINVAL 22

int
select(int nfds, fd_set * __restrict readfds, fd_set * __restrict writefds,
    fd_set * __restrict exceptfds, struct timeval * __restrict timeout)
{
	if (nfds > FD_SETSIZE) {
		errno = EINVAL;
		return -1;
	}
	return __select_nocancel(nfds, readfds, writefds, exceptfds, timeout);
}
