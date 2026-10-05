/*
 * Minimal xf86drm.h for the libdrm shim (v3d_drm.c). Only what Mesa's vc4
 * driver and the DRM uapi macros it uses need; the real libdrm header is
 * not available on Darwin. Mesa's include/drm-uapi/drm.h provides the ioctl
 * numbers and struct definitions (DRM_IOCTL_GEM_CLOSE...).
 */
#ifndef XF86DRM_H_V3D_SHIM
#define XF86DRM_H_V3D_SHIM

#include <stdint.h>
#include <stddef.h>
#include <sys/ioctl.h>
#include "drm-uapi/drm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _drmVersion {
	int version_major;
	int version_minor;
	int version_patchlevel;
	int name_len;
	char *name;
	int date_len;
	char *date;
	int desc_len;
	char *desc;
} drmVersion, *drmVersionPtr;

int drmIoctl(int fd, unsigned long request, void *arg);
int drmGetCap(int fd, uint64_t capability, uint64_t *value);
drmVersionPtr drmGetVersion(int fd);
void drmFreeVersion(drmVersionPtr v);

int drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle);
int drmSyncobjDestroy(int fd, uint32_t handle);
int drmSyncobjImportSyncFile(int fd, uint32_t handle, int sync_file_fd);
int drmSyncobjExportSyncFile(int fd, uint32_t handle, int *sync_file_fd);
int drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle);
int drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd);

#ifdef __cplusplus
}
#endif
#endif
