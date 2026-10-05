/*
 * BCM2837V3D user-client ABI (docs/gpu-opengl-plan.md, DARW-295).
 *
 * Plain C, no kernel headers: shared by the kext and by userland
 * (Mesa's Darwin vc4 winsys, DARW-296, and the bring-up tools).
 *
 * The shapes follow Linux's include/uapi/drm/vc4_drm.h (the DRM_IOCTL_VC4_*
 * structs) so that the Mesa vc4 gallium driver's winsys layer maps one-to-one
 * onto IOConnectCallMethod selectors. This file is an independent
 * definition, not a copy: selector numbers, struct padding and the
 * mapping model (IOConnectMapMemory64 type == BO handle, instead of
 * DRM_IOCTL_VC4_MMAP_BO + mmap(offset)) are this project's own.
 *
 * Every struct is explicitly 8-byte aligned and padded, because it is
 * passed as an IOConnectCallStructMethod blob and must have the same
 * layout for an arm64 client and the arm64 kernel.
 */
#ifndef _BCM2837V3DUAPI_H
#define _BCM2837V3DUAPI_H

#include <stdint.h>

#define kBCM2837V3DServiceName "BCM2837V3D"

/* IOConnectCallMethod selectors */
enum {
	kV3DMethodGetParam = 0,    /* in: v3d_get_param      out: v3d_get_param */
	kV3DMethodCreateBO,        /* in: v3d_create_bo      out: v3d_create_bo */
	kV3DMethodCloseBO,         /* in: scalar handle */
	kV3DMethodWaitSeqno,       /* in: v3d_wait_seqno */
	kV3DMethodWaitBO,          /* in: v3d_wait_bo */
	kV3DMethodSubmitCL,        /* in/out: v3d_submit_cl (validated; DARW-295 phase 2) */
	kV3DMethodGetHangState,    /* out: v3d_hang_state */
	kV3DMethodRawSubmit,       /* in/out: v3d_raw_submit (root + boot-arg v3d-raw=1) */
	kV3DMethodGetBOAddress,    /* in: scalar handle  out: scalar GPU address (root only) */
	kV3DMethodCount
};

/*
 * IOConnectMapMemory64(connect, type = BO handle, ...) maps a BO into the
 * caller. BOs are mapped write-combined (the GPU reads them with no CPU
 * cache snooping, so a cacheable CPU mapping would be incoherent).
 */

/* v3d_get_param.param; values mirror DRM_VC4_PARAM_* */
enum {
	kV3DParamIdent0 = 0,
	kV3DParamIdent1,
	kV3DParamIdent2,
	kV3DParamSupportsBranches,
	kV3DParamSupportsETC1,
	kV3DParamSupportsThreadedFS,
	kV3DParamSupportsFixedRCLOrder,
	kV3DParamSupportsMadvise,
	kV3DParamSupportsPerfmon,
};

struct v3d_get_param {
	uint32_t param;
	uint32_t pad;
	uint64_t value;
};

struct v3d_create_bo {
	uint32_t size;      /* bytes, rounded up to a page by the kernel */
	uint32_t flags;     /* must be 0 */
	uint32_t handle;    /* out */
	uint32_t pad;
};

struct v3d_wait_seqno {
	uint64_t seqno;
	uint64_t timeout_ns;
};

struct v3d_wait_bo {
	uint32_t handle;
	uint32_t pad;
	uint64_t timeout_ns;
};

/* Same field set as struct drm_vc4_submit_rcl_surface */
struct v3d_submit_rcl_surface {
	uint32_t hindex;    /* index into bo_handles, or ~0 */
	uint32_t offset;
	uint16_t bits;
	uint16_t flags;
	uint32_t pad;
};

/*
 * Validated submit. Same inputs as struct drm_vc4_submit_cl; user pointers
 * are 64-bit. Not implemented yet (returns kIOReturnUnsupported): it needs
 * the command-list, shader-record and QPU-shader validators that rewrite
 * handle references into GPU addresses and bound every GPU access
 * (Linux: vc4_validate.c, vc4_validate_shaders.c, vc4_render_cl.c).
 */
struct v3d_submit_cl {
	uint64_t bin_cl;
	uint64_t shader_rec;
	uint64_t uniforms;
	uint64_t bo_handles;
	uint32_t bin_cl_size;
	uint32_t shader_rec_size;
	uint32_t shader_rec_count;
	uint32_t uniforms_size;
	uint32_t bo_handle_count;
	uint16_t width;
	uint16_t height;
	uint8_t min_x_tile, min_y_tile, max_x_tile, max_y_tile;
	struct v3d_submit_rcl_surface color_read, color_write, zs_read, zs_write,
	    msaa_color_write, msaa_zs_write;
	uint32_t clear_color[2];
	uint32_t clear_z;
	uint8_t clear_s;
	uint8_t pad8[3];
	uint32_t flags;
	uint32_t pad32;
	uint64_t seqno;     /* out */
};

/*
 * Raw submit: bring-up only. The caller supplies GPU (bus) addresses of
 * hand-built control lists, already relocated. NOT VALIDATED: the GPU can
 * DMA anywhere the bus allows, so it is restricted to root and to
 * boot-arg v3d-raw=1. bo_handles keeps the named BOs alive until the job
 * completes. Either list may be empty (start == end).
 */
#define V3D_RAW_MAX_BOS 16
struct v3d_raw_submit {
	uint32_t bin_start, bin_end;
	uint32_t render_start, render_end;
	uint32_t bo_count;
	uint32_t bo_handles[V3D_RAW_MAX_BOS];
	uint32_t pad;
	uint64_t seqno;     /* out */
};

/* Registers captured by the hang detector, as struct drm_vc4_get_hang_state */
struct v3d_hang_state {
	uint32_t valid;     /* 0 if no hang has occurred */
	uint32_t start_bin, start_render;
	uint32_t ct0ca, ct0ea, ct1ca, ct1ea;
	uint32_t ct0cs, ct1cs, ct0ra0, ct1ra0;
	uint32_t bpca, bpcs, bpoa, bpos;
	uint32_t vpmbase, dbge, fdbgo, fdbgb, fdbgr, fdbgs, errstat;
	uint32_t pad;
	uint64_t seqno;
};

#endif /* _BCM2837V3DUAPI_H */
