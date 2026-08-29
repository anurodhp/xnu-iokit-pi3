/* Real, from-source implementations of the small subset of
 * IOKitLib.c (third_party/IOKitUser/IOKitLib.c) real launchctl.c calls
 * directly (IOKitWaitQuiet/IOObjectRelease/kIOMasterPortDefault in
 * system_specific_bootstrap()/do_crash_debug_mode2()/
 * do_single_user_mode2(); IORegistryEntryFromPath/
 * IORegistryEntryCreateCFProperty in do_bootroot_magic()) --
 * build_iokituser.sh's own libiokituser.a is deliberately narrow (just
 * the real MIG client stubs + a couple of low-level wrappers, see that
 * script's own header comment), it doesn't compile IOKitLib.c itself
 * (a real, structural CF/dispatch/xpc/ASL dependency wall when that
 * script was first written -- now substantially closed by this
 * project's later real CoreFoundation/libxpc/libdispatch ports, but
 * still a large, separate undertaking to port IOKitLib.c's full ~3300
 * lines, out of scope for this launchctl-focused pass).
 *
 * kIOMasterPortDefault/IOMasterPort/IOObjectRelease/IOKitWaitQuiet/
 * IORegistryEntryFromPath below are REAL, copied faithfully from
 * IOKitLib.c's own real bodies (see that file's own lines ~110-124,
 * 206-211, 1372-1396, 2179-2201) -- every symbol they call
 * (mach_host_self, host_get_io_master, mach_port_deallocate,
 * io_registry_entry_from_path, io_service_wait_quiet) is already real
 * and already linked (libmach_ipc.a, libiokituser.a's own real
 * iokitmig64.o MIG client stubs).
 *
 * IORegistryEntryCreateCFProperty is the one REAL, DOCUMENTED gap here:
 * real IOKitLib.c's own body (line ~2457) calls
 * IORegistryEntrySearchCFProperty(), which needs io_registry_entry_get_
 * property_bin()/IOCFUnserializeWithSize() -- the latter lives in
 * IOCFSerialize.c/IOCFUnserialize.tab.c, real IOKitUser source files
 * this project has never compiled (same class of gap as the rest of
 * IOKitLib.c). Returning NULL here is honest, not fabricated: this
 * function's only real caller in launchctl.c is do_bootroot_magic()
 * (checks IODeviceTree:/chosen's "bootroot-active" property, a real
 * Apple-desktop BootCache/Boot-Root feature with zero applicability to
 * this project's rpi3/SD-card boot design -- do_bootroot_magic() is
 * itself only ever reached from system_specific_bootstrap(), the
 * `launchctl bootstrap -S System` internal self-bootstrap path real
 * launchd used to invoke via itself at boot on real Darwin; this
 * project's own launchd fork instead auto-loads /Library/LaunchDaemons
 * directly via pd_launchd_load_daemons_dir(), see third_party-patches/
 * PureDarwin.patch, so this code path is never actually exercised here).
 */
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <IOKit/IOTypes.h>
#include <IOKit/IOKitKeys.h>
#include <CoreFoundation/CoreFoundation.h>

extern kern_return_t io_registry_entry_from_path(mach_port_t master_port, io_string_t path, mach_port_t *registry_entry);
extern kern_return_t io_service_wait_quiet(mach_port_t service, mach_timespec_t wait_time);
extern kern_return_t host_get_io_master(host_t host, mach_port_t *io_master);

const mach_port_t kIOMasterPortDefault = MACH_PORT_NULL;

kern_return_t
IOMasterPort(mach_port_t bootstrapPort __attribute__((unused)), mach_port_t *masterPort)
{
	mach_port_t host_port;
	kern_return_t result;

	host_port = mach_host_self();
	result = host_get_io_master(host_port, masterPort);
	if (host_port != MACH_PORT_NULL) {
		mach_port_deallocate(mach_task_self(), host_port);
	}
	return result;
}

static mach_port_t
__IOGetDefaultMasterPort(void)
{
	mach_port_t masterPort;
	kern_return_t result = IOMasterPort(MACH_PORT_NULL, &masterPort);
	if (KERN_SUCCESS != result) {
		masterPort = MACH_PORT_NULL;
	}
	return masterPort;
}

kern_return_t
IOObjectRelease(io_object_t object)
{
	return mach_port_deallocate(mach_task_self(), object);
}

io_registry_entry_t
IORegistryEntryFromPath(mach_port_t _masterPort, const io_string_t path)
{
	kern_return_t kr;
	io_registry_entry_t entry;
	mach_port_t masterPort;

	if (MACH_PORT_NULL == _masterPort) {
		masterPort = __IOGetDefaultMasterPort();
	} else {
		masterPort = _masterPort;
	}

	kr = io_registry_entry_from_path(masterPort, (char *)path, &entry);
	if (kIOReturnSuccess != kr) {
		entry = 0;
	}

	if ((masterPort != MACH_PORT_NULL) && (masterPort != _masterPort)) {
		mach_port_deallocate(mach_task_self(), masterPort);
	}

	return entry;
}

kern_return_t
IOKitWaitQuiet(mach_port_t _masterPort, mach_timespec_t *waitTime)
{
	io_service_t root;
	kern_return_t kr;
	mach_timespec_t defaultWait = { 0, -1 };
	mach_port_t masterPort;

	if (MACH_PORT_NULL == _masterPort) {
		masterPort = __IOGetDefaultMasterPort();
	} else {
		masterPort = _masterPort;
	}

	kr = io_registry_entry_from_path(masterPort, kIOServicePlane ":/", &root);

	if (kr == KERN_SUCCESS) {
		if (0 == waitTime) {
			waitTime = &defaultWait;
		}
		kr = io_service_wait_quiet(root, *waitTime);
		IOObjectRelease(root);
	}

	if ((masterPort != MACH_PORT_NULL) && (masterPort != _masterPort)) {
		mach_port_deallocate(mach_task_self(), masterPort);
	}

	return kr;
}

/* Real, documented gap -- see this file's own header comment. */
CFTypeRef
IORegistryEntryCreateCFProperty(io_registry_entry_t entry __attribute__((unused)),
    CFStringRef key __attribute__((unused)),
    CFAllocatorRef allocator __attribute__((unused)),
    uint32_t options __attribute__((unused)))
{
	return NULL;
}
