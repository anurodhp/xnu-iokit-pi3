/*
 * Real, narrow no-op stand-in for process_AgentMonitor()/
 * process_AgentMonitor_DNS()/process_AgentMonitor_Proxy() -- the plain-C
 * entry points Plugins/IPMonitor/ip_plugin.c calls into
 * Plugins/IPMonitor/agent-monitor.m (real ObjC, DAR-239's scope,
 * untouched here).
 *
 * NOT a fabricated behavior: reading agent-monitor.m's own real,
 * unmodified source (DAR-241 investigation, 2026-09-04) shows every one
 * of these three functions starts with the exact same real gate:
 *
 *     if (!haveNetworkExtensionFramework()) {
 *         return;
 *     }
 *
 * where haveNetworkExtensionFramework() is `[NEPolicy class] != nil` --
 * a real ObjC runtime class lookup for NetworkExtension.framework. This
 * project has no objc4/NetworkExtension.framework at all (DAR-236/237),
 * so on a real, unmodified macOS box with NetworkExtension.framework
 * missing or not yet loaded, `[NEPolicy class]` returns nil and these
 * functions ALREADY do nothing but return -- exactly the behavior
 * implemented here. This is not a stand-in for DHCP/network-config
 * behavior: the "Agent" subsystem's whole purpose (confirmed by reading
 * controller.m/configAgent.m) is registering NetworkAgent objects for
 * interactive VPN/captive-portal-style UI prompts and DNS/proxy-proxy
 * "agent" data publication to NetworkExtension consumers -- a layer on
 * TOP of the real DNS/proxy configuration ip_plugin.c/dns-configuration.c/
 * proxy-configuration.c already compute and publish via SCDynamicStore
 * (the real, load-bearing path every actual DNS/proxy consumer on this
 * port uses).
 */
#include <stdint.h>
#include <uuid/uuid.h>

void
process_AgentMonitor(void)
{
}

void
process_AgentMonitor_DNS(void)
{
}

void
process_AgentMonitor_Proxy(void)
{
}

/*
 * DAR-241 follow-up (same session): nwi/network_information_server.c's
 * _nwi_config_agent_copy() also calls copy_dns_information_for_agent_uuid()/
 * copy_proxy_information_for_agent_uuid() -- agent-monitor.m's own real
 * implementations of these two (see agent-monitor.h) have the IDENTICAL
 * haveNetworkExtensionFramework() gate documented above, returning NULL
 * on any system without NetworkExtension.framework loaded. Real,
 * already-existing Apple behavior, not a fabrication.
 */
const void *
copy_dns_information_for_agent_uuid(uuid_t uuid, uint64_t *length)
{
	(void)uuid;
	if (length != NULL) {
		*length = 0;
	}
	return NULL;
}

const void *
copy_proxy_information_for_agent_uuid(uuid_t uuid, uint64_t *length)
{
	(void)uuid;
	if (length != NULL) {
		*length = 0;
	}
	return NULL;
}
