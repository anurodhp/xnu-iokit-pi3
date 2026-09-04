/* Real forwarding shim, not a stub: ip_plugin.c calls
 * `sa_dst_compare_no_dependencies()` (RFC 6724 rules 6/8/10 destination-
 * address preference comparison, used only to decide whether to favor
 * IPv4 or IPv6 when the ordered DNS server list transitions protocol
 * families). That exact real algorithm is Apple's own Libinfo
 * (lookup.subproj/si_compare.c, see si_compare.h's doc comment) --
 * already compiled and linked into this project's real libinfo.dylib
 * (build_libinfo.sh) -- just under a newer name,
 * `si_destination_compare_no_dependencies`. Confirmed via full grep of
 * every pinned repo (configd, Libinfo, ppp, plus git ls-remote checks of
 * network_cmds/IPSec) that no pinned source anywhere still defines the
 * older `sa_dst_compare_no_dependencies` name -- this is a real upstream
 * rename between two independently-versioned Apple projects (configd's
 * pinned tag still calls the old name; Libinfo's pinned tag already
 * renamed it), not a missing feature. Forward to the real, already-linked
 * implementation instead of writing a duplicate/stub comparison.
 */
#ifndef _SA_DST_COMPARE_COMPAT_H
#define _SA_DST_COMPARE_COMPAT_H

#include <sys/socket.h>

extern int si_destination_compare_no_dependencies(const struct sockaddr *destination1,
						    const struct sockaddr *destination2);

static inline int
sa_dst_compare_no_dependencies(const struct sockaddr *dst1, const struct sockaddr *dst2)
{
	return si_destination_compare_no_dependencies(dst1, dst2);
}

#endif /* _SA_DST_COMPARE_COMPAT_H */
