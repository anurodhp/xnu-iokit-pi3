/* Real `union sockaddr_in_4_6` (verbatim body from
 * third_party/xnu-7195/bsd/netinet/in.h:824, real Apple source) --
 * needed by that same file's own bsd/netinet/in_var.h a few hundred
 * lines later (struct kev_socket_event_data), but this project's
 * pinned iPhoneOS14.4 SDK ships its own older netinet/in.h under the
 * SAME include guard (_NETINET_IN_H_), found first via the normal
 * sysroot search -- so xnu's real, newer copy (which has this union)
 * never actually gets read; the guard is already tripped. Same
 * "SDK's own same-named older header wins" class of gap this project
 * has hit before (mach/mach_vm.h, mach/mach_time.h) -- force-included
 * here rather than fighting search-path precedence, since only this one
 * missing type (not the whole newer in.h) is actually needed.
 */
#ifndef _SOCKADDR_IN_4_6_COMPAT_H_
#define _SOCKADDR_IN_4_6_COMPAT_H_

#include <sys/socket.h>
#include <netinet/in.h>

#ifndef __SOCKADDR_IN_4_6_DEFINED__
#define __SOCKADDR_IN_4_6_DEFINED__
union sockaddr_in_4_6 {
	struct sockaddr         sa;
	struct sockaddr_in      sin;
	struct sockaddr_in6     sin6;
};
#endif

#endif /* _SOCKADDR_IN_4_6_COMPAT_H_ */
