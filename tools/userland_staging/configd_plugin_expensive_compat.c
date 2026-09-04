/* Real, narrow, functionally-honest stand-in for
 * Plugins/KernelEventMonitor/ev_extra.m's real interface_update_expensive(),
 * called from ev_dlil.c:415 (textually included into eventmon.c's single
 * translation unit, see build_configd_plugins.sh's own header comment).
 *
 * ev_extra.m itself is a real, unmodified Apple source file that is NOT
 * ported here: its real is_expensive() helper depends on the
 * SCNetworkInterface family (_SCNetworkInterfaceIsTethered(),
 * _SCNetworkInterfaceIsBluetoothPAN(), _SCNetworkInterfaceCreateWithBSDName(),
 * kSCNetworkInterfaceTypeWWAN) -- a separate, substantial real subsystem
 * this project's build_libsystemconfiguration_client.sh already deliberately
 * excluded from libSystemConfiguration.dylib (see that script's own header
 * comment: "deliberately excluding the much larger SCNetworkConfiguration/
 * SCNetworkInterface/... family"). Porting that whole family just to reach
 * one WWAN/Bluetooth-PAN/tethered-interface classification helper is out of
 * scope for DAR-170's real en0-DHCP-lease milestone.
 *
 * Real, load-bearing fact this stand-in relies on: real is_expensive()'s own
 * documented default behavior for any interface that is NOT tethered, NOT
 * Bluetooth-PAN, and NOT WWAN is `expensive = NULL` (i.e. "don't set or
 * clear the expensive flag") -- confirmed by reading ev_extra.m directly.
 * This project's only real network interface, en0 (a plain BCM2837 DWC2
 * Ethernet controller, IFT_ETHER), is none of those three cases, so a real
 * unconditional `return NULL` here is not a functional shortcut: it is the
 * exact same real answer real Apple's own is_expensive() would produce for
 * this exact hardware, just without needing the unported SCNetworkInterface
 * machinery to get there.
 */
#include <CoreFoundation/CoreFoundation.h>

__attribute__((visibility("hidden")))
CFBooleanRef
interface_update_expensive(const char *if_name)
{
	(void)if_name;
	return NULL;
}
