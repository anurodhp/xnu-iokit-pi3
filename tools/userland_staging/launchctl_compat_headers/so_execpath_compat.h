/* Real `SO_EXECPATH` (verbatim value from
 * third_party/xnu-7195/bsd/sys/socket.h:201, real Apple source, "Application
 * Firewall Socket option") -- needed by real launchctl.c's own
 * do_application_firewall_magic(), which sets it via setsockopt(). This
 * project's pinned iPhoneOS14.4 SDK ships its own older sys/socket.h
 * under the SAME include guard (_SYS_SOCKET_H_), found first via the
 * normal sysroot search -- so xnu's real, newer copy (which has this
 * macro) never actually gets read; the guard is already tripped. Same
 * "SDK's own same-named older header wins" class of gap as
 * sockaddr_in_4_6_compat.h in this same directory tree -- force-included
 * here rather than fighting search-path precedence, since only this one
 * missing macro (not the whole newer socket.h) is actually needed.
 */
#ifndef _SO_EXECPATH_COMPAT_H_
#define _SO_EXECPATH_COMPAT_H_

#include <sys/socket.h>

#ifndef SO_EXECPATH
#define SO_EXECPATH 0x1085
#endif

#endif /* _SO_EXECPATH_COMPAT_H_ */
