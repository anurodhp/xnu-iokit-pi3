#ifndef _NW_PRIVATE_COMPAT_H
#define _NW_PRIVATE_COMPAT_H

#include <stdint.h>
#include <dispatch/dispatch.h>

/* Real compat stand-in for <nw/private.h> (libnetcore's Network.framework
 * internal header, never open-sourced -- confirmed via full pinned-repo grep).
 * Plugins/KernelEventMonitor/eventmon.c #includes this but, confirmed by grep,
 * never references any "nw_" or "NW_" prefixed symbol anywhere in the real
 * file -- vestigial for that file's real compiled surface (original finding,
 * still true).
 *
 * DAR-241 (2026-09-04): Plugins/IPMonitor/ip_plugin.c also #includes this
 * (transitively, via "SCNetworkReachabilityInternal.h") and DOES need real
 * type names to exist -- SCNetworkReachabilityInternal.h's own
 * SCNetworkReachabilityRef struct definition has nw_endpoint_t/
 * nw_parameters_t/nw_path_evaluator_t/nw_path_t/nw_resolver_t/
 * nw_resolver_status_t/nw_array_t fields. Confirmed by grep: ip_plugin.c
 * itself never reads or writes any of those fields (they belong to
 * SCNetworkReachability's own real implementation in SCNetworkReachability.c,
 * which this project has not ported/compiled here) -- ip_plugin.c only
 * needs the struct to be well-formed so it can #include the header for
 * unrelated declarations. Opaque pointer typedefs are the honest, narrow
 * fix: real byte layout/semantics are never exercised by any code this
 * project actually compiles or runs.
 */
typedef struct nw_endpoint		*nw_endpoint_t;
typedef struct nw_parameters		*nw_parameters_t;
typedef struct nw_path_evaluator	*nw_path_evaluator_t;
typedef struct nw_path			*nw_path_t;
typedef struct nw_resolver		*nw_resolver_t;
typedef struct nw_resolver_status	*nw_resolver_status_t;
typedef struct nw_array		*nw_array_t;

/* DAR-241 (2026-09-04): Plugins/IPMonitor/nat64-configuration.c needs
 * real libnetcore/Network.framework NAT64-prefix-discovery types+API
 * (RFC 7050 PLAT/well-known-prefix discovery via DNS64, `nw_nat64_
 * prefixes_resolver_*`) that this project has no implementation of --
 * Network.framework itself is a much larger, separate, never-open-
 * sourced subsystem (no dyld/objc4-independent path to it exists here).
 *
 * Real scope split confirmed by reading the whole file: only the
 * DISCOVERY half (_nat64_resolver_create() and everything it drives)
 * depends on Network.framework. The half that actually programs the
 * kernel once a prefix is known (_nat64_prefix_set(), via the real,
 * already-present SIOCSIFNAT64PREFIX ioctl + struct if_nat64req in
 * xnu-7195's bsd/net/if.h) is genuine, real, unmodified kernel-facing
 * code and is NOT touched by this compat layer.
 *
 * Real-behavior justification for a no-op discovery resolver: NAT64/
 * DNS64 PLAT-prefix discovery only matters on an IPv6-only network that
 * needs to reach IPv4-only destinations through a NAT64 gateway. This
 * project's real target is a single Ethernet interface doing real IPv4
 * DHCP (DAR-170) -- there is no IPv6-only/NAT64 gateway scenario to
 * discover a prefix for. Making nw_interface_create_with_index() return
 * NULL drives _nat64_resolver_create() into ITS OWN existing, real
 * "interface lookup failed" early-return path (nat64-configuration.c
 * ~line 203) -- the exact same real code Apple's own binary would run
 * on a lookup failure -- so NAT64PrefixRequestStart() never registers an
 * update handler and the feature self-disables cleanly. Not a functional
 * shortcut for DHCP/network-config-relevant behavior: DHCP/basic IPv4
 * connectivity never touches this file at all.
 */
typedef struct nw_interface			*nw_interface_t;
typedef struct nw_nat64_prefixes_resolver	*nw_nat64_prefixes_resolver_t;

#define NW_NAT64_PREFIX_STR_LENGTH	64

typedef struct nw_nat64_prefix {
	uint8_t		data[12];	/* real Apple layout: up to a /96 prefix's network bits (data[12]), confirmed by nat64-configuration.c's own "MIN(16, 12)" comment at its one real memcpy site */
	uint8_t		length;
} nw_nat64_prefix_t;

typedef void (^nw_nat64_copy_prefixes_block_t)(int32_t num_prefixes, nw_nat64_prefix_t *prefixes);

nw_parameters_t nw_parameters_create(void);
nw_interface_t nw_interface_create_with_index(uint32_t if_index);
void nw_parameters_require_interface(nw_parameters_t parameters, nw_interface_t interface);
void nw_parameters_set_required_address_family(nw_parameters_t parameters, int address_family);
void nw_release(void *object);
void nw_nat64_write_prefix_to_string(nw_nat64_prefix_t *prefix, char *str, size_t str_len);
nw_nat64_prefixes_resolver_t nw_nat64_prefixes_resolver_create(nw_parameters_t parameters);
void nw_nat64_prefixes_resolver_set_cancel_handler(nw_nat64_prefixes_resolver_t resolver, dispatch_block_t handler);
void nw_nat64_prefixes_resolver_set_update_handler(nw_nat64_prefixes_resolver_t resolver, dispatch_queue_t queue, nw_nat64_copy_prefixes_block_t handler);
void nw_nat64_prefixes_resolver_start(nw_nat64_prefixes_resolver_t resolver);
void nw_nat64_prefixes_resolver_cancel(nw_nat64_prefixes_resolver_t resolver);

#endif /* _NW_PRIVATE_COMPAT_H */
