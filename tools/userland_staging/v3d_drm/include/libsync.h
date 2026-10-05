/* libsync.h for the libdrm shim: sync files are not supported (see v3d_drm.c). */
#ifndef LIBSYNC_H_V3D_SHIM
#define LIBSYNC_H_V3D_SHIM
#ifdef __cplusplus
extern "C" {
#endif
int sync_wait(int fd, int timeout);
int sync_accumulate(const char *name, int *fd1, int fd2);
#ifdef __cplusplus
}
#endif
#endif
