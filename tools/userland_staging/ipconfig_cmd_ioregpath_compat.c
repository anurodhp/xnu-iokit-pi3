/* DAR-170: real Apple `ipconfig`(8) client -- narrow, honestly-documented
 * stand-in for the one bootplib/ioregpath.c entry point client.c references
 * (myIORegistryEntryCopyValue; confirmed via `nm -u client.o`, the other two
 * ioregpath.c functions are never reached from this binary).
 *
 * Why a stand-in and not the real file: ioregpath.c is real Apple source and
 * compiles fine, but its whole body is calls into the high-level
 * IOKitLib.framework C API (IORegistryEntryFromPath, IOMasterPort,
 * IORegistryEntryCreateCFProperties, IOObjectRelease, ...). This project has
 * no IOKitLib: build_iokituser.sh ships only the real *MIG transport*
 * (libiokituser.dylib exports the io_* MIG routines and nothing else --
 * confirmed via `nm -gU`), and IOKitLib.c itself has been repeatedly found not
 * to compile here (see configd_builtin_plugins_stub.c's own header comment for
 * the last real, timeboxed attempt and the ticket filed for it). Real Apple's
 * own SystemConfiguration.framework hits the same wall gracefully by design --
 * dy_framework.c dlopen()s IOKit.framework at runtime and returns NULL/failure
 * when it is absent, which is exactly what this port observes at boot
 * ("could not get IOMasterPort, kr = 0x5" in /var/log/configd.log).
 *
 * What is actually lost: ipconfig(8)'s NetBoot-only paths. This function is
 * reached ONLY from client.c's `netbootoption` and `netbootpacket`
 * subcommands, which read the BSDP/NetBoot response the *boot ROM* left behind
 * in the IORegistry (IODeviceTree:/chosen). This target has no NetBoot boot
 * ROM and no such IORegistry property, so a real ioregpath.c linked here would
 * return NULL for these paths too. Returning NULL is therefore the honest
 * answer, not a papered-over gap: `ipconfig netbootpacket` prints its own real
 * "netboot packet not available" message, unchanged.
 *
 * Nothing on the DHCP path this ticket is about (`ipconfig set en0 DHCP`,
 * `ipconfig getpacket en0`, `ipconfig getoption`, `ipconfig waitall`) touches
 * any of these -- confirmed by reading every call site in client.c.
 */
#include <CoreFoundation/CoreFoundation.h>

CFDictionaryRef
myIORegistryEntryCopyValue(const char * path)
{
	(void)path;
	return (NULL);
}
