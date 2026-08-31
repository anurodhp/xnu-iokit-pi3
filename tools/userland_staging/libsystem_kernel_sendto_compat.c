/* Real, curated sendto()/send() -- added 2026-08-31 (static-linking
 * cleanup, dylib-vs-dylib duplicate-symbol pass, BSD socket family).
 *
 * sendto(): same real curation this project already established in
 * xpc_syscall_wrappers_compat.c for connect()/recvmsg()/sendmsg() --
 * Apple's own real source (libsyscall/wrappers/legacy/sendto.c)
 * unconditionally `#define _NONSTD_SOURCE` before including
 * <sys/socket.h> et al, which this project's pinned iOS 14.4 SDK
 * rejects on an LP64/arm64 (__DARWIN_ONLY_UNIX_CONFORMANCE) target
 * (confirmed via a real trial compile, not guessed -- same wall
 * xpc_syscall_wrappers_compat.c's own header already documents). This
 * is the real logic that file implements (a 10-line pass-through to
 * the real `__sendto_nocancel` raw trap, confirmed by reading it
 * directly), curated without the incompatible preprocessor scaffolding
 * -- not reinvented, not simplified in behavior.
 *
 * send(): has no dedicated kernel syscall on real Darwin either --
 * third_party/Libc/net/FreeBSD/send.c's real body is just
 * `return sendto(s, msg, len, flags, NULL, 0);` (same real semantics
 * tools/userland_staging/libresolv_socket_syscalls.c's own header
 * already documents for its now-superseded private copy). Reproduced
 * here as a thin real wrapper over this file's own sendto() -- real
 * Apple's actual layering, not a shortcut.
 */
#include <sys/types.h>
#include <sys/socket.h>

extern ssize_t __sendto_nocancel(int, const void *, size_t, int, const struct sockaddr *, socklen_t);
extern int *__error(void); /* real libc errno-location func, already linked */
#define errno (*__error())
#define EOPNOTSUPP 102
#define ENOTSUP 45

ssize_t
sendto(int s, const void *msg, size_t len, int flags, const struct sockaddr *to, socklen_t tolen)
{
	ssize_t ret = __sendto_nocancel(s, msg, len, flags, to, tolen);
	if (ret < 0 && errno == EOPNOTSUPP) {
		errno = ENOTSUP;
	}
	return ret;
}

ssize_t
send(int s, const void *msg, size_t len, int flags)
{
	return sendto(s, msg, len, flags, (const struct sockaddr *)0, 0);
}
