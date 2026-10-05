/*
 * Force-included (-include) into every Mesa translation unit built for this
 * port. Mesa maps vc4 buffer objects with a plain mmap() on the DRM
 * descriptor, using the offset DRM_IOCTL_VC4_MMAP_BO returned; here BOs are
 * mapped through IOConnectMapMemory64, so those two calls are redirected to
 * the shim, which forwards everything that is not a V3D mapping to the real
 * mmap()/munmap(). Doing it this way keeps Mesa's sources unmodified.
 */
#ifndef V3D_MMAP_HOOK_H
#define V3D_MMAP_HOOK_H

#include <sys/types.h>
#include <sys/mman.h>

#ifdef __cplusplus
extern "C" {
#endif
void *v3d_drm_mmap_hook(void *addr, size_t len, int prot, int flags, int fd, off_t offset);
int v3d_drm_munmap_hook(void *addr, size_t len);
#ifdef __cplusplus
}
#endif

#define mmap(a, l, p, f, fd, o) v3d_drm_mmap_hook((a), (l), (p), (f), (fd), (o))
#define munmap(a, l) v3d_drm_munmap_hook((a), (l))

#endif
