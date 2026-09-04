/*
 * Real, narrow no-op definitions for the handful of libnetcore/
 * Network.framework "nw_nat64_*"/"nw_interface_*"/"nw_parameters_*"
 * symbols Plugins/IPMonitor/nat64-configuration.c calls to discover a
 * NAT64/DNS64 PLAT prefix (RFC 7050) -- see configd_plugin_compat/
 * nw/private.h's own header comment (DAR-241, 2026-09-04) for the full
 * real-behavior justification: this project's target is a single
 * Ethernet interface doing real IPv4 DHCP, never an IPv6-only/NAT64
 * gateway network, so discovering a PLAT prefix is a real, genuinely
 * inapplicable subsystem here -- NOT a stand-in for DHCP/network-config
 * correctness. The kernel-facing half of nat64-configuration.c that
 * actually programs a discovered prefix onto an interface
 * (_nat64_prefix_set(), via the real SIOCSIFNAT64PREFIX ioctl) is real,
 * untouched, unmodified code -- this file only neuters the
 * Network.framework-dependent discovery half.
 *
 * nw_interface_create_with_index() returning NULL drives
 * nat64-configuration.c's own _nat64_resolver_create() into ITS OWN
 * real, already-existing "interface lookup failed" early-return path,
 * so NAT64PrefixRequestStart() never registers an update handler and no
 * resolver is ever created -- every other function here is consequently
 * unreachable at runtime, but still needs a real, linkable definition
 * since this project compiles at -O0 (no dead-code-driven symbol
 * elimination to rely on).
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <dispatch/dispatch.h>
#include <nw/private.h>

nw_parameters_t
nw_parameters_create(void)
{
	return (nw_parameters_t)NULL;
}

nw_interface_t
nw_interface_create_with_index(uint32_t if_index)
{
	(void)if_index;
	return (nw_interface_t)NULL;
}

void
nw_parameters_require_interface(nw_parameters_t parameters, nw_interface_t interface)
{
	(void)parameters;
	(void)interface;
}

void
nw_parameters_set_required_address_family(nw_parameters_t parameters, int address_family)
{
	(void)parameters;
	(void)address_family;
}

void
nw_release(void *object)
{
	(void)object;
}

void
nw_nat64_write_prefix_to_string(nw_nat64_prefix_t *prefix, char *str, size_t str_len)
{
	(void)prefix;
	if (str != NULL && str_len > 0) {
		str[0] = '\0';
	}
}

nw_nat64_prefixes_resolver_t
nw_nat64_prefixes_resolver_create(nw_parameters_t parameters)
{
	(void)parameters;
	return (nw_nat64_prefixes_resolver_t)NULL;
}

void
nw_nat64_prefixes_resolver_set_cancel_handler(nw_nat64_prefixes_resolver_t resolver, dispatch_block_t handler)
{
	(void)resolver;
	(void)handler;
}

void
nw_nat64_prefixes_resolver_set_update_handler(nw_nat64_prefixes_resolver_t resolver, dispatch_queue_t queue, nw_nat64_copy_prefixes_block_t handler)
{
	(void)resolver;
	(void)queue;
	(void)handler;
}

void
nw_nat64_prefixes_resolver_start(nw_nat64_prefixes_resolver_t resolver)
{
	(void)resolver;
}

void
nw_nat64_prefixes_resolver_cancel(nw_nat64_prefixes_resolver_t resolver)
{
	(void)resolver;
}
