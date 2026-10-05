/*
 * v3dprobe: first-contact tool for the BCM2837V3D kext (DARW-294/295).
 *
 *   v3dprobe            report idents, allocate/map a BO, upload a shader,
 *                       then run a render-only job (kernel-generated render
 *                       list that clears a 64x64 linear RGBA8888 target) and
 *                       check every pixel.
 *   v3dprobe -q         device query only
 *
 * Exit status 0 only if everything that was attempted passed. On a machine
 * without a V3D (QEMU) it reports that and exits 2.
 *
 * Uses only the validated path: no boot-arg or privilege needed.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "v3d_drm.h"
#include "drm-uapi/vc4_drm.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf(" (errno %d: %s)\n", errno, strerror(errno)); } else { printf("ok:   " __VA_ARGS__); printf("\n"); } } while (0)

static uint64_t
param(int fd, uint32_t p)
{
	struct drm_vc4_get_param g = { .param = p };
	if (drmIoctl(fd, DRM_IOCTL_VC4_GET_PARAM, &g) != 0) {
		return ~0ull;
	}
	return g.value;
}

int
main(int argc, char **argv)
{
	int query_only = argc > 1 && strcmp(argv[1], "-q") == 0;
	int fd = v3d_drm_open();
	if (fd < 0) {
		printf("v3dprobe: cannot open BCM2837V3D (%s): %s\n", strerror(errno),
		    errno == ENODEV ? "no V3D service (not a Pi 3, or the driver declined; see the boot log)" : "");
		return 2;
	}

	uint64_t id0 = param(fd, DRM_VC4_PARAM_V3D_IDENT0), id1 = param(fd, DRM_VC4_PARAM_V3D_IDENT1);
	printf("V3D_IDENT0 %#llx  IDENT1 %#llx  (rev %llu, %llu slices, %llu QPUs/slice)\n",
	    (unsigned long long)id0, (unsigned long long)id1, (unsigned long long)(id1 & 0xf),
	    (unsigned long long)((id1 >> 4) & 0xf), (unsigned long long)((id1 >> 8) & 0xf));
	CHECK(id0 == 0x02443356, "IDENT0 reads 'V3D' with version 2");
	printf("supports: branches %llu, ETC1 %llu, threaded FS %llu, fixed RCL order %llu\n",
	    (unsigned long long)param(fd, DRM_VC4_PARAM_SUPPORTS_BRANCHES),
	    (unsigned long long)param(fd, DRM_VC4_PARAM_SUPPORTS_ETC1),
	    (unsigned long long)param(fd, DRM_VC4_PARAM_SUPPORTS_THREADED_FS),
	    (unsigned long long)param(fd, DRM_VC4_PARAM_SUPPORTS_FIXED_RCL_ORDER));
	if (query_only) {
		v3d_drm_close(fd);
		return fails != 0;
	}

	/* BO round trip through the mapping path */
	struct drm_vc4_create_bo cb = { .size = 4096 };
	CHECK(drmIoctl(fd, DRM_IOCTL_VC4_CREATE_BO, &cb) == 0, "create 4 KiB BO");
	struct drm_vc4_mmap_bo mb = { .handle = cb.handle };
	CHECK(drmIoctl(fd, DRM_IOCTL_VC4_MMAP_BO, &mb) == 0, "mmap token");
	volatile uint32_t *p = v3d_drm_mmap(fd, 4096, mb.offset);
	CHECK(p != (void *)-1, "map BO");
	if (p != (void *)-1) {
		for (int i = 0; i < 1024; i++) p[i] = 0xc0de0000u + i;
		int bad = 0;
		for (int i = 0; i < 1024; i++) bad += p[i] != 0xc0de0000u + i;
		CHECK(bad == 0, "write-combined BO reads back what was written");
		v3d_drm_munmap(fd, (void *)p, 4096);
	}

	/* shader BO: a program that does nothing and ends */
	uint64_t nop_end[4];
	/* nop = sig NONE, add/mul waddr NOP, cond ALWAYS, raddr NOP; prog_end sets sig 3 */
	const uint64_t nop = (1ull << 60) | (39ull << 38) | (39ull << 32) | (1ull << 49) | (1ull << 46) | (39ull << 18) | (39ull << 12);
	nop_end[0] = nop; nop_end[1] = (nop & ~(0xfull << 60)) | (3ull << 60); nop_end[2] = nop; nop_end[3] = nop;
	struct drm_vc4_create_shader_bo sb = { .size = sizeof(nop_end), .data = (uintptr_t)nop_end };
	CHECK(drmIoctl(fd, DRM_IOCTL_VC4_CREATE_SHADER_BO, &sb) == 0, "validated shader BO accepted");

	/* render-only clear: kernel generates the render list */
	const unsigned W = 64, H = 64;
	struct drm_vc4_create_bo tb = { .size = W * H * 4 };
	CHECK(drmIoctl(fd, DRM_IOCTL_VC4_CREATE_BO, &tb) == 0, "create 64x64 target");
	struct drm_vc4_mmap_bo tm = { .handle = tb.handle };
	drmIoctl(fd, DRM_IOCTL_VC4_MMAP_BO, &tm);
	volatile uint32_t *px = v3d_drm_mmap(fd, W * H * 4, tm.offset);
	CHECK(px != (void *)-1, "map target");
	if (px != (void *)-1) {
		for (unsigned i = 0; i < W * H; i++) px[i] = 0xdeadbeef;
		uint32_t handles[1] = { tb.handle };
		struct drm_vc4_submit_cl s;
		memset(&s, 0, sizeof(s));
		s.bo_handles = (uintptr_t)handles;
		s.bo_handle_count = 1;
		s.width = W; s.height = H;
		s.color_write.hindex = 0;
		s.color_write.bits = (1 << 2);   /* VC4_RENDER_CONFIG_FORMAT_RGBA8888 (1) << FORMAT_SHIFT (2); linear memory format */
		s.color_read.hindex = s.zs_read.hindex = s.zs_write.hindex = ~0u;
		s.msaa_color_write.hindex = s.msaa_zs_write.hindex = ~0u;
		s.flags = VC4_SUBMIT_CL_USE_CLEAR_COLOR;
		s.clear_color[0] = s.clear_color[1] = 0xff336699;
		CHECK(drmIoctl(fd, DRM_IOCTL_VC4_SUBMIT_CL, &s) == 0, "submit clear job (seqno %llu)", (unsigned long long)s.seqno);
		struct drm_vc4_wait_seqno w = { .seqno = s.seqno, .timeout_ns = 2000000000ull };
		CHECK(drmIoctl(fd, DRM_IOCTL_VC4_WAIT_SEQNO, &w) == 0, "job completed within 2 s");
		unsigned wrong = 0;
		for (unsigned i = 0; i < W * H; i++) wrong += px[i] != 0xff336699;
		CHECK(wrong == 0, "all %u pixels cleared to 0xff336699 (%u wrong, first px %#x)", W * H, wrong, px[0]);
		struct drm_vc4_get_hang_state hs;
		if (drmIoctl(fd, DRM_IOCTL_VC4_GET_HANG_STATE, &hs) == 0) {
			printf("note: the GPU hung since boot (ct0ca %#x ct1ca %#x errstat %#x)\n", hs.ct0ca, hs.ct1ca, hs.errstat);
		}
		v3d_drm_munmap(fd, (void *)px, W * H * 4);
	}

	v3d_drm_close(fd);
	printf(fails ? "v3dprobe: FAIL (%d)\n" : "v3dprobe: PASS\n", fails);
	return fails != 0;
}
