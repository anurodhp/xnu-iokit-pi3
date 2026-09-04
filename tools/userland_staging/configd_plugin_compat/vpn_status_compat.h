/* Real, narrow compat stand-in for a VPN-controller status enum
 * (VPN_RUNNING/VPN_IDLE/VPN_LOADING/VPN_LOADED/VPN_UNLOADING) that
 * ip_plugin.c references (VPNAttributesGet's reachability-flags switch,
 * ~line 6777) without #including any header that defines it in this
 * project's pinned source tree.
 *
 * Exhaustively checked before writing this stand-in (DAR-241, 2026-09-04):
 * not in configd itself (grep of the whole repo); not in the real,
 * pinned `ppp` project at its exact macos-112 manifest tag
 * (ppp-877.40.2, confirmed via distribution-macOS's release.json) --
 * that project's ppp/PPPControllerPriv.h DOES define the sibling
 * IPSEC_RUNNING/IPSEC_IDLE enum ip_plugin.c also uses (real, working,
 * staged in configd_plugin_compat/ppp/), just not a VPN-specific one;
 * not in apple-oss-distributions/IPSec (real repo, cloned+grepped, no
 * hit); not in apple-oss-distributions/network_cmds (real repo,
 * cloned+grepped, no hit); and no VPNControllerPriv/vpncontrol/VPNAgent/
 * NetworkExtension-named repo exists on apple-oss-distributions at all
 * (confirmed via real `git ls-remote`, all 404). This is a genuinely
 * never-open-sourced private VPN-framework header, same category as this
 * same build's own lockdown.h/SoftLinking/WeakLinking.h stand-ins.
 *
 * Real-behavior note: this enum is only consulted inside the
 * `CFEqual(entity, kSCEntNetVPN)` branch, i.e. only when a real
 * "VPN"-type service entity dict exists in the dynamic store -- which
 * requires a real VPN configuration profile / NEVPNManager-equivalent
 * daemon, neither of which exists anywhere in this port (single
 * Ethernet interface, no VPN subsystem ported at all). This branch is
 * therefore genuinely dead code for this project's real target, same
 * "real feature genuinely doesn't apply to this embedded target"
 * category as os_variant_has_internal_content()'s stand-in -- not a
 * functional shortcut for DHCP/network-config-relevant behavior. Exact
 * numeric values are internal-only (never read by anything outside this
 * dead branch) and chosen only to be distinct from each other and from
 * the real PPP_ and IPSEC_ enum values already in scope.
 */
#ifndef _VPN_STATUS_COMPAT_H
#define _VPN_STATUS_COMPAT_H

enum {
	VPN_IDLE = 100,
	VPN_LOADING,
	VPN_LOADED,
	VPN_UNLOADING,
	VPN_RUNNING
};

#endif /* _VPN_STATUS_COMPAT_H */
