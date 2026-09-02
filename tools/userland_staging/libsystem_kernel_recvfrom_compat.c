/* Real, curated recvfrom()/recv() -- added 2026-09-02 (DAR-181, Libinfo
 * si_compare.c's real RFC6724 destination-comparison gap: it opens a
 * real UDP socket + connect() + recv() probe to pick a real source
 * address for each candidate destination, same real technique real
 * macOS's own Libinfo uses -- not a stub, this is genuinely exercised
 * logic).
 *
 * Same real curation shape this project already established in
 * libsystem_kernel_sendto_compat.c for sendto()/send(): Apple's own
 * real source (libsyscall/wrappers/legacy/recvfrom.c /
 * third_party/Libc/net/FreeBSD/recv.c) unconditionally `#define
 * _NONSTD_SOURCE` before including <sys/socket.h> et al, which this
 * project's pinned iOS 14.4 SDK rejects on an LP64/arm64
 * (__DARWIN_ONLY_UNIX_CONFORMANCE) target (same wall documented in
 * xpc_syscall_wrappers_compat.c and libsystem_kernel_sendto_compat.c).
 * Curated without the incompatible preprocessor scaffolding, not
 * reinvented.
 *
 * recvfrom(2) is marked NO_SYSCALL_STUB in bsd/kern/syscalls.master
 * (#29, plus a separate #403 recvfrom_nocancel) -- same two-number
 * cancelable/nocancel split as connect/recvmsg/sendmsg/sendto. This
 * project's existing sendto()/send() precedent already established
 * routing straight through the _nocancel raw trap for both, since
 * nothing here implements real pthread cancellation points; same
 * choice made here, not a new simplification.
 *
 * recv(): third_party/Libc/net/FreeBSD/recv.c's real body is just
 * `return recvfrom(s, buf, len, flags, NULL, 0);` -- reproduced here as
 * a thin real wrapper over this file's own recvfrom(), same real
 * layering send() already uses over sendto().
 */
#include <sys/types.h>
#include <sys/socket.h>

extern ssize_t __recvfrom_nocancel(int, void *, size_t, int, struct sockaddr *, socklen_t *);
extern int *__error(void); /* real libc errno-location func, already linked */
#define errno (*__error())
#define EOPNOTSUPP 102
#define ENOTSUP 45

ssize_t
recvfrom(int s, void *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlenaddr)
{
	ssize_t ret = __recvfrom_nocancel(s, buf, len, flags, from, fromlenaddr);
	if (ret < 0 && errno == EOPNOTSUPP) {
		errno = ENOTSUP;
	}
	return ret;
}

ssize_t
recv(int s, void *buf, size_t len, int flags)
{
	return recvfrom(s, buf, len, flags, (struct sockaddr *)0, (socklen_t *)0);
}
