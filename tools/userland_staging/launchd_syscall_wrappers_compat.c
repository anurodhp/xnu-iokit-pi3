/* Real, curated accept()/listen()/socketpair() -- verbatim logic from
 * third_party/xnu-7195/libsyscall/wrappers/legacy/{accept,listen,
 * socketpair}.c (real Apple source), minus the `_NONSTD_SOURCE`/
 * `#include "_errno.h"` preprocessor scaffolding those files use --
 * real, confirmed-by-trial-compile incompatible with this project's
 * LP64/arm64-only `__DARWIN_ONLY_UNIX_CONFORMANCE` target (same
 * "legacy wrapper files aren't meant to build standalone on this exact
 * architecture" finding already established for connect()/recvmsg()/
 * sendmsg() in xpc_syscall_wrappers_compat.c -- real Apple's own
 * arm64/iOS libsyscall build excludes these files per-arch too, for
 * the same reason).
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>

extern int __accept_nocancel(int, struct sockaddr *, socklen_t *);
extern int __listen(int, int);
extern int __socketpair(int, int, int, int[2]);

int
accept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
	int ret = __accept_nocancel(s, addr, addrlen);
	if (ret < 0 && errno == EOPNOTSUPP) {
		errno = ENOTSUP;
	}
	return ret;
}

int
listen(int socket, int backlog)
{
	int ret = __listen(socket, backlog);
	if (ret < 0 && errno == EOPNOTSUPP) {
		errno = ENOTSUP;
	}
	return ret;
}

int
socketpair(int domain, int type, int protocol, int socket_vector[2])
{
	int ret = __socketpair(domain, type, protocol, socket_vector);
	if (ret < 0 && errno == EOPNOTSUPP) {
		errno = ENOTSUP;
	}
	return ret;
}
