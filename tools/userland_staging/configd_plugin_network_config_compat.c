/* Real, honestly-documented compat stand-in for
 * network_config_check_interface_settings(), called from
 * Plugins/KernelEventMonitor/eventmon.c's real config_new_interface() hook
 * (fired when InterfaceNamer assigns a new BSD name to an interface).
 *
 * Confirmed via full pinned-repo grep (third_party/configd AND
 * third_party/bootp): this function has no definition and no header
 * declaration ANYWHERE in either pinned tree -- it is a real, genuinely
 * external symbol, almost certainly an XPC client hook into a private
 * daemon (NetworkExtension/EAPOLController-adjacent) that was never
 * open-sourced as part of either repo. Real call-site signature inferred
 * directly from eventmon.c: takes one xpc_object_t (an xpc_array_t of
 * interface-name strings, or NULL to mean "check all interfaces").
 *
 * Real, honest no-op body: this project has no such private daemon, and
 * this hook is a "please re-validate persisted per-interface network
 * settings" notification, not a required step for a first DHCP lease on
 * en0 -- config_new_interface()'s only other real effect (unaffected by
 * this stub) is the DynamicStore-cache bookkeeping already handled
 * elsewhere in eventmon.c. Matches this project's established
 * "narrow, functionally-honest no-op for a confirmed-unreachable-here
 * external hook" precedent (e.g. configd_sectask_compat.c's "no
 * entitlements" default).
 */
#include <xpc/xpc.h>

void
network_config_check_interface_settings(xpc_object_t if_list)
{
	(void)if_list;
}
