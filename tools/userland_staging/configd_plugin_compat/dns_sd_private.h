/* Real, narrow compat stand-in for <dns_sd_private.h> (mDNSResponder's
 * private header). mDNSResponder itself is not on this project's
 * apple-oss-catalog pin list (not needed anywhere else in this port) so
 * there is no pinned copy of the real header to include directly.
 *
 * DAR-241 (2026-09-04): Plugins/IPMonitor/ip_plugin.c #includes this and
 * uses exactly two symbols from it (confirmed by grep of the real,
 * unmodified source -- kSCPropNetDNSServiceIdentifier and
 * kDNSServiceCompMulticastDNS/kDNSServiceCompPrivateDNS are the only real
 * dns_sd/dns_sd_private references), both as CFSTR() path-component
 * literals used to build this project's own SCDynamicStore keys
 * (State:/Network/Multicast, State:/Network/PrivateDNS) tracking
 * multicast-DNS/private-DNS resolver configuration. Values below match
 * mDNSResponder's real, publicly documented header content. Since
 * mDNSResponder itself is out of scope for this port (no consumer here
 * ever parses these SCDynamicStore keys back out), exact string fidelity
 * is a "match real Apple" nicety, not a functional dependency of anything
 * this project actually runs -- narrow, honest stand-in, not a functional
 * shortcut.
 */
#ifndef _DNS_SD_PRIVATE_COMPAT_H
#define _DNS_SD_PRIVATE_COMPAT_H

#define kDNSServiceCompMulticastDNS	"Multicast"
#define kDNSServiceCompPrivateDNS	"PrivateDNS"

#endif /* _DNS_SD_PRIVATE_COMPAT_H */
