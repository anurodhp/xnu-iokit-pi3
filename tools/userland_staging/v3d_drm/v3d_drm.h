/*
 * libdrm-compatible shim for the BCM2837V3D kext (docs/gpu-opengl-plan.md,
 * DARW-296).
 *
 * Mesa's vc4 gallium driver talks to the kernel through drmIoctl() on a DRM
 * file descriptor and mmap()s buffer objects with offsets that come from an
 * ioctl. This Darwin port has no DRM character device; the GPU is an IOKit
 * service. This shim keeps Mesa's side unchanged where it can:
 *
 *   - v3d_drm_open() returns a real, harmless file descriptor (so close(),
 *     dup-free bookkeeping and "fd >= 0" checks in callers behave) and
 *     attaches an IOKit connection to it;
 *   - drmIoctl() decodes the DRM request number and forwards the vc4
 *     ioctls to the kext's user-client methods (BCM2837V3DUAPI.h, whose
 *     structs are byte-identical to struct drm_vc4_*, see
 *     tools/v3d_validate_test/abi_check.c);
 *   - DRM_IOCTL_VC4_MMAP_BO returns an opaque token in `offset`; the caller
 *     must use v3d_drm_mmap() instead of mmap(): the kernel maps BOs
 *     through IOConnectMapMemory64, not through the descriptor.
 *
 * Not supported (fail with ENOSYS, as vc4 treats these as optional): flink,
 * prime import/export, syncobjs, perfmon.
 *
 * The core is transport-independent (struct v3d_transport) so the host
 * tests can run it against a mock kernel.
 */
#ifndef V3D_DRM_H
#define V3D_DRM_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What the shim needs from the kernel connection. All "int" results are
 * IOReturn values (0 = success). */
struct v3d_transport {
	void *ctx;
	int (*call_struct)(void *ctx, uint32_t selector, const void *in, size_t in_size,
	    void *out, size_t *out_size);
	int (*call_scalar)(void *ctx, uint32_t selector, const uint64_t *in, uint32_t n_in,
	    uint64_t *out, uint32_t *n_out);
	/* Maps the BO named by `handle` into this task. */
	int (*map_bo)(void *ctx, uint32_t handle, uint64_t *addr, uint64_t *size);
	int (*unmap_bo)(void *ctx, uint32_t handle, uint64_t addr);
	void (*close)(void *ctx);
};

/* Registers `t` and returns a descriptor for it, or -1 with errno set. */
int v3d_drm_open_transport(const struct v3d_transport *t);

/* Opens the BCM2837V3D IOKit service (v3d_drm_iokit.c). -1/errno on failure,
 * e.g. ENODEV when the machine has no V3D. */
int v3d_drm_open(void);

/* Releases the connection and closes the descriptor. */
int v3d_drm_close(int fd);

/* libdrm entry points (prototypes in include/xf86drm.h): drmIoctl, drmGetCap,
 * drmGetVersion/drmFreeVersion, and ENOSYS stubs for syncobjs and prime. */

/* mmap() replacement for BOs: `offset` is the token drmIoctl returned from
 * DRM_IOCTL_VC4_MMAP_BO. Returns MAP_FAILED-style (void *)-1 on error. */
void *v3d_drm_mmap(int fd, size_t length, uint64_t offset);
int v3d_drm_munmap(int fd, void *addr, size_t length);

/* Redirection targets for Mesa's mmap()/munmap(); see include/v3d_mmap_hook.h. */
void *v3d_drm_mmap_hook(void *addr, size_t len, int prot, int flags, int fd, off_t offset);
int v3d_drm_munmap_hook(void *addr, size_t len);

/* IOReturn -> errno, exposed for tests. */
int v3d_drm_ioreturn_to_errno(int ioreturn);

#ifdef __cplusplus
}
#endif
#endif
