/*
 * shutdown(8) -s (sleep) on this port (DAR-334).
 *
 * shutdown.c's only IOKit use is its sleep branch
 * (third_party/system_cmds/shutdown.tproj/shutdown.c:463-477): IOMasterPort(),
 * then IOPMFindPowerManagement() and IOPMSleepSystem() from IOKitUser's
 * pwr_mgt.subproj/IOPMLib.c:37-54 and :95-107. Sleep cannot happen here:
 * BCM2837PlatformExpert implements no sleep or wake
 * (iokit/BCM2837PlatformExpert/, only reset and halt, DAR-334), so
 * IOPMrootDomain has no platform path into a sleep state.
 *
 * (Not "IOKitLib is impossible": libiokituser.dylib carries the iokit MIG
 * client, and launchctl_iokit_compat.c already builds IOMasterPort()/
 * IORegistryEntryFromPath() on it. What is missing is anything to sleep.)
 *
 * So IOMasterPort() fails with a message saying so, and shutdown.c then
 * skips the sleep (it only proceeds when IOMasterPort() succeeds). The other
 * two are defined for the link and are not reached. shutdown -r and -h do not
 * touch any of this; they go to launchd through reboot3() (shutdown.c:498).
 */
#include <stdio.h>
#include <mach/mach.h>

typedef mach_port_t io_connect_t;
typedef int IOReturn;
#define kIOReturnUnsupported ((IOReturn)0xe00002c7) /* xnu iokit/IOKit/IOReturn.h:153, iokit_common_err(0x2c7) */

kern_return_t IOMasterPort(mach_port_t bootstrapPort, mach_port_t *masterPort);
io_connect_t IOPMFindPowerManagement(mach_port_t master_device_port);
IOReturn IOPMSleepSystem(io_connect_t fb);

kern_return_t
IOMasterPort(mach_port_t bootstrapPort, mach_port_t *masterPort)
{
	(void)bootstrapPort;
	if (masterPort != NULL) {
		*masterPort = MACH_PORT_NULL;
	}
	fprintf(stderr, "shutdown: system sleep is not supported on this hardware\n");
	return kIOReturnUnsupported;
}

io_connect_t
IOPMFindPowerManagement(mach_port_t master_device_port)
{
	(void)master_device_port;
	return MACH_PORT_NULL;
}

IOReturn
IOPMSleepSystem(io_connect_t fb)
{
	(void)fb;
	return kIOReturnUnsupported;
}
