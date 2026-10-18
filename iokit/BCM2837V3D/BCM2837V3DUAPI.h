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
 * Every struct is explicitly padded, because it is passed as an
 * IOConnectCallStructMethod blob and must have the same layout for an
 * arm64 client and the arm64 kernel. v3d_get_param, v3d_create_bo,
 * v3d_wait_*, v3d_create_shader_bo, v3d_submit_rcl_surface and
 * v3d_submit_cl are byte-identical to their struct drm_vc4_* counterparts
 * in Linux's vc4_drm.h (checked by tools/v3d_validate_test/abi_check.c), so
 * the libdrm shim passes Mesa's structs straight through.
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
	kV3DMethodCreateShaderBO,  /* in/out: v3d_create_shader_bo */
	kV3DMethodReadBO,          /* in: v3d_read_bo (fast CPU read-back, see the struct) */
	kV3DMethodShareBO,         /* in: v3d_share_bo (back a SysV segment with the BO) */
	kV3DMethodPrepareRead,     /* in: v3d_prepare_read (before a shared BO is read) */
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

/* v3d_submit_rcl_surface.flags */
#define V3D_SUBMIT_RCL_SURFACE_READ_IS_FULL_RES (1u << 0)
/* v3d_submit_cl.flags */
#define V3D_SUBMIT_CL_USE_CLEAR_COLOR           (1u << 0)
#define V3D_SUBMIT_CL_FIXED_RCL_ORDER           (1u << 1)
#define V3D_SUBMIT_CL_RCL_ORDER_INCREASING_X    (1u << 2)
#define V3D_SUBMIT_CL_RCL_ORDER_INCREASING_Y    (1u << 3)

/* Same field set as struct drm_vc4_submit_rcl_surface */
struct v3d_submit_rcl_surface {
	uint32_t hindex;    /* index into bo_handles, or ~0 */
	uint32_t offset;
	uint16_t bits;
	uint16_t flags;
};

/*
 * Validated submit. Same inputs as struct drm_vc4_submit_cl; user pointers
 * are 64-bit addresses in the calling task. The kernel copies the lists in,
 * validates them with the vendored VC4 validators (validate/), rewrites
 * handle references into GPU addresses, generates the render list itself,
 * and queues the job. bo_handles is an array of bo_handle_count uint32
 * handles (this connection's handle space); command lists name BOs by index
 * into it.
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
	uint64_t seqno;     /* out */
	/* The remaining fields exist for layout compatibility with
	 * struct drm_vc4_submit_cl and are ignored (no perfmon, no syncobjs). */
	uint32_t perfmonid;
	uint32_t in_sync;
	uint32_t out_sync;
	uint32_t pad2;
};

/*
 * Shader BOs hold QPU programs. The kernel validates the program once at
 * creation (a program that could read or write outside the buffers a job
 * names is refused) and the BO is immutable afterwards: it cannot be
 * mapped, and it can only be used as a shader, never as data. `data` is the
 * address of `size` bytes (a multiple of 8) in the calling task.
 */
struct v3d_create_shader_bo {
	uint32_t size;
	uint32_t flags;     /* must be 0 */
	uint64_t data;
	uint32_t handle;    /* out */
	uint32_t pad;
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

/*
 * Read `size` bytes at `offset` of a BO into `dst` (an address in the
 * calling task), after the GPU has finished with it. This is the fast way
 * to read GPU output on the CPU: user mappings of BOs are write-combined,
 * and CPU reads through them are uncached (about 49 MB/s on a Pi 3 against
 * about 330 MB/s for ordinary memory). The kernel cleans and invalidates
 * the data cache for the range and copies it out through a cached mapping.
 * Not for shader BOs. No DRM equivalent (Linux has no such ioctl; the vc4
 * display path scans out instead).
 */
struct v3d_read_bo {
	uint32_t handle;
	uint32_t offset;
	uint32_t size;
	uint32_t pad;
	uint64_t dst;
};

/*
 * Zero-copy present (iokit docs/video-decode.md): make the SysV segment
 * `shmid` -- created by the caller with shmget(), attached by nobody yet --
 * the BO's own pages, mapped cacheable wherever the segment is attached
 * (the BO's other mappings stay write-combined). An X server given the
 * segment with XShmAttach then reads the GPU's output in place, with no
 * read-back copy. The caller must own the segment (IPC_M), and the BO must
 * be at least the segment's size; not for shader BOs. The segment keeps the
 * BO's memory until it is destroyed, whatever happens to the handle.
 *
 * Coherence: the GPU writes RAM and no CPU writes through the segment, so
 * no line is ever dirty, but a reader's cached lines go stale when the GPU
 * renders into the BO again. Before each read (each XShmPutImage) call
 * kV3DMethodPrepareRead: it waits for the BO's last job, then cleans and
 * invalidates the range from every core's cache.
 */
struct v3d_share_bo {
	uint32_t handle;
	int32_t shmid;
};

struct v3d_prepare_read {
	uint32_t handle;
	uint32_t offset;
	uint32_t size;
	uint32_t pad;
};

#endif /* _BCM2837V3DUAPI_H */
