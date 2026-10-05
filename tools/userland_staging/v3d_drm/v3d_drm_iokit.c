/*
 * IOKit transport of the libdrm shim: connects to the BCM2837V3D service
 * and forwards calls to its user-client methods (BCM2837V3DUAPI.h).
 */
#include <errno.h>
#include <stdlib.h>
#include <mach/mach.h>
#include <IOKit/IOKitLib.h>

#include "v3d_drm.h"
#include "BCM2837V3DUAPI.h"

struct iok {
	io_connect_t conn;
};

static int
t_call_struct(void *ctx, uint32_t sel, const void *in, size_t in_size, void *out, size_t *out_size)
{
	struct iok *k = ctx;
	return IOConnectCallStructMethod(k->conn, sel, in, in_size, out, out_size);
}

static int
t_call_scalar(void *ctx, uint32_t sel, const uint64_t *in, uint32_t n_in, uint64_t *out, uint32_t *n_out)
{
	struct iok *k = ctx;
	return IOConnectCallScalarMethod(k->conn, sel, in, n_in, out, n_out);
}

/* The BO handle doubles as the memory "type": the kext's
 * clientMemoryForType() looks the buffer up in this connection's table. */
static int
t_map_bo(void *ctx, uint32_t handle, uint64_t *addr, uint64_t *size)
{
	struct iok *k = ctx;
	mach_vm_address_t a = 0;
	mach_vm_size_t s = 0;
	kern_return_t kr = IOConnectMapMemory64(k->conn, handle, mach_task_self(), &a, &s, kIOMapAnywhere);
	if (kr == 0) {
		*addr = a;
		*size = s;
	}
	return kr;
}

static int
t_unmap_bo(void *ctx, uint32_t handle, uint64_t addr)
{
	struct iok *k = ctx;
	return IOConnectUnmapMemory64(k->conn, handle, mach_task_self(), addr);
}

static void
t_close(void *ctx)
{
	struct iok *k = ctx;
	IOServiceClose(k->conn);
	free(k);
}

int
v3d_drm_open(void)
{
	io_service_t svc = IOServiceGetMatchingService(kIOMasterPortDefault,
	    IOServiceMatching(kBCM2837V3DServiceName));
	if (svc == 0) {
		errno = ENODEV;
		return -1;
	}
	struct iok *k = calloc(1, sizeof(*k));
	if (k == NULL) {
		IOObjectRelease(svc);
		errno = ENOMEM;
		return -1;
	}
	kern_return_t kr = IOServiceOpen(svc, mach_task_self(), 0, &k->conn);
	IOObjectRelease(svc);
	if (kr != 0) {
		free(k);
		errno = v3d_drm_ioreturn_to_errno(kr);
		return -1;
	}
	struct v3d_transport t = { k, t_call_struct, t_call_scalar, t_map_bo, t_unmap_bo, t_close };
	int fd = v3d_drm_open_transport(&t);
	if (fd < 0) {
		int e = errno;
		IOServiceClose(k->conn);
		free(k);
		errno = e;
	}
	return fd;
}
