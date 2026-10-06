/*
 * v3d_membench: CPU read/write bandwidth of a V3D buffer object's user
 * mapping (write-combined) against ordinary malloc'd memory. Used to size
 * the GL present path (mesa_xlib_vc4/xlib_vc4.c), which reads every frame
 * back from a BO. Prints MB/s for memcpy BO->RAM, RAM->RAM and RAM->BO.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "v3d_drm.h"
#include "xf86drm.h"
#include "drm-uapi/vc4_drm.h"

static double
now_s(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void
bench(const char *what, void *dst, const void *src, size_t n, int reps)
{
	memcpy(dst, src, n);   /* warm */
	double t = now_s();
	for (int i = 0; i < reps; i++)
		memcpy(dst, src, n);
	t = now_s() - t;
	printf("%-12s %7.1f MB/s\n", what, (double)n * reps / t / 1e6);
}

int
main(void)
{
	const size_t n = 4 << 20;
	int fd = v3d_drm_open();
	if (fd < 0) {
		printf("no V3D\n");
		return 1;
	}
	struct drm_vc4_create_bo cb = { .size = n };
	if (drmIoctl(fd, DRM_IOCTL_VC4_CREATE_BO, &cb) != 0) {
		printf("create BO failed\n");
		return 1;
	}
	struct drm_vc4_mmap_bo mb = { .handle = cb.handle };
	drmIoctl(fd, DRM_IOCTL_VC4_MMAP_BO, &mb);
	void *bo = v3d_drm_mmap(fd, n, mb.offset);
	if (bo == (void *)-1) {
		printf("map failed\n");
		return 1;
	}
	void *a = malloc(n), *b = malloc(n);
	memset(a, 1, n);
	memset(b, 2, n);
	bench("BO -> RAM", a, bo, n, 4);
	bench("RAM -> RAM", a, b, n, 16);
	bench("RAM -> BO", bo, b, n, 16);
	v3d_drm_munmap(fd, bo, n);
	v3d_drm_close(fd);
	return 0;
}
