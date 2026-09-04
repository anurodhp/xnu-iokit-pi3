/* Real declaration paired with configd_plugin_network_config_compat.c's
 * real, honestly-documented no-op definition -- see that file's own header
 * comment for the full real gap explanation. -include'd globally by
 * build_configd_plugins.sh so eventmon.c's real, unmodified call sites
 * type-check without needing a source patch.
 */
#ifndef CONFIGD_PLUGIN_NETWORK_CONFIG_CHECK_COMPAT_H
#define CONFIGD_PLUGIN_NETWORK_CONFIG_CHECK_COMPAT_H

#include <xpc/xpc.h>

extern void network_config_check_interface_settings(xpc_object_t if_list);

#endif
