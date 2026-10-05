/*
 * Mock BCM2837V3D kernel for the host tests: implements the user-client
 * methods with the same argument checks as BCM2837V3DUserClient::
 * externalMethod, runs submissions through the real validators (v3d_kglue.h)
 * and completes jobs instantly (there is no GPU model). Shared by
 * test_drm_shim.c and test_mesa_vc4.c.
 */
#ifndef MOCK_KERNEL_H
#define MOCK_KERNEL_H
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <IOKit/IOReturn.h>

#include "v3d_drm.h"
#include "BCM2837V3DUAPI.h"
#include "v3d_kglue.h"

/* ----------------------------- mock kernel ----------------------------- */

#define KMAX 1025
struct kbo {
	int used;
	uint32_t size;          /* page rounded */
	uint32_t gpu;
	void *mem;
	struct vc4_validated_shader_info *shader;
};
struct kernel {
	int submits, rejected_submits, shaders, rejected_shaders;
	int jobs_with_bin, jobs_with_recs, jobs_render_only;
	uint64_t uniform_bytes, bin_bytes;
	int corrupt_next;          /* test hook: damage the next bin CL */
	struct kbo bo[KMAX];
	uint64_t next_seqno, finished;
	uint32_t next_gpu;
	int open_maps;
};

struct jobbuf { void *staging; };

static int
k_alloc(void *ctx, size_t size, bool stage, uint32_t *gpu, void **staging)
{
	struct kernel *k = ctx;
	*gpu = k->next_gpu;
	k->next_gpu += (uint32_t)((size + 0xfff) & ~0xfffu) + 0x1000;
	*staging = stage ? calloc(1, (size + 15) & ~(size_t)15) : NULL;
	return 0;
}

static int
k_call_struct(void *ctx, uint32_t sel, const void *in, size_t in_size, void *out, size_t *out_size)
{
	struct kernel *k = ctx;
	switch (sel) {
	case kV3DMethodGetParam: {
		if (in_size != sizeof(struct v3d_get_param) || !out || *out_size != sizeof(struct v3d_get_param)) return kIOReturnBadArgument;
		struct v3d_get_param p = *(const struct v3d_get_param *)in;
		switch (p.param) {
		case kV3DParamIdent0: p.value = 0x02443356; break;
		case kV3DParamIdent1: p.value = 0x1111; break;
		case kV3DParamIdent2: p.value = 0; break;
		case kV3DParamSupportsBranches: case kV3DParamSupportsETC1:
		case kV3DParamSupportsThreadedFS: case kV3DParamSupportsFixedRCLOrder: p.value = 1; break;
		case kV3DParamSupportsMadvise: case kV3DParamSupportsPerfmon: p.value = 0; break;
		default: return kIOReturnBadArgument;
		}
		memcpy(out, &p, sizeof(p));
		return 0;
	}
	case kV3DMethodCreateBO: {
		if (in_size != sizeof(struct v3d_create_bo) || !out || *out_size != sizeof(struct v3d_create_bo)) return kIOReturnBadArgument;
		const struct v3d_create_bo *c = in;
		if (c->flags || !c->size || c->size > 16u << 20) return kIOReturnBadArgument;
		for (int i = 0; i < KMAX - 1; i++) {
			if (!k->bo[i].used) {
				uint32_t sz = (c->size + 0xfff) & ~0xfffu;
				k->bo[i] = (struct kbo){ 1, sz, k->next_gpu, calloc(1, sz), NULL };
				k->next_gpu += sz + 0x1000;
				struct v3d_create_bo o = { sz, 0, (uint32_t)i + 1, 0 };
				memcpy(out, &o, sizeof(o));
				return 0;
			}
		}
		return kIOReturnNoSpace;
	}
	case kV3DMethodCreateShaderBO: {
		if (in_size != sizeof(struct v3d_create_shader_bo) || !out || *out_size != sizeof(struct v3d_create_shader_bo)) return kIOReturnBadArgument;
		const struct v3d_create_shader_bo *c = in;
		if (c->flags || !c->size || (c->size & 7) || c->size > 65536) return kIOReturnBadArgument;
		struct vc4_validated_shader_info *si = v3d_kglue_validate_shader((const void *)(uintptr_t)c->data, c->size);
		if (!si) { k->rejected_shaders++; return kIOReturnNotPermitted; }
		k->shaders++;
		for (int i = 0; i < KMAX - 1; i++) {
			if (!k->bo[i].used) {
				uint32_t sz = (c->size + 0xfff) & ~0xfffu;
				k->bo[i] = (struct kbo){ 1, sz, k->next_gpu, calloc(1, sz), si };
				k->next_gpu += sz + 0x1000;
				memcpy(k->bo[i].mem, (const void *)(uintptr_t)c->data, c->size);
				struct v3d_create_shader_bo o = *c; o.handle = (uint32_t)i + 1;
				memcpy(out, &o, sizeof(o));
				return 0;
			}
		}
		v3d_kglue_free_shader(si);
		return kIOReturnNoSpace;
	}
	case kV3DMethodWaitSeqno: {
		if (in_size != sizeof(struct v3d_wait_seqno)) return kIOReturnBadArgument;
		const struct v3d_wait_seqno *w = in;
		return w->seqno <= k->finished ? 0 : kIOReturnTimeout;
	}
	case kV3DMethodWaitBO: {
		if (in_size != sizeof(struct v3d_wait_bo)) return kIOReturnBadArgument;
		const struct v3d_wait_bo *w = in;
		if (!w->handle || w->handle >= KMAX || !k->bo[w->handle - 1].used) return kIOReturnNotFound;
		return 0;
	}
	case kV3DMethodGetHangState: {
		if (!out || *out_size != sizeof(struct v3d_hang_state)) return kIOReturnBadArgument;
		memset(out, 0, sizeof(struct v3d_hang_state));
		return 0;
	}
	case kV3DMethodSubmitCL: {
		if (in_size != sizeof(struct v3d_submit_cl) || !out || *out_size != sizeof(struct v3d_submit_cl)) return kIOReturnBadArgument;
		struct v3d_submit_cl a = *(const struct v3d_submit_cl *)in;
		if (a.bo_handle_count > 1024) return kIOReturnBadArgument;
		uint32_t *handles = (uint32_t *)(uintptr_t)a.bo_handles;
		struct v3d_kbo_desc d[64];
		if (a.bo_handle_count > 64) return kIOReturnBadArgument;
		for (uint32_t i = 0; i < a.bo_handle_count; i++) {
			uint32_t h = handles[i];
			if (!h || h >= KMAX || !k->bo[h - 1].used) return kIOReturnNotFound;
			d[i] = (struct v3d_kbo_desc){ k->bo[h - 1].size, k->bo[h - 1].gpu, k->bo[h - 1].shader };
		}
		if (k->corrupt_next && a.bin_cl_size) { ((uint8_t *)(uintptr_t)a.bin_cl)[0] = 200; k->corrupt_next = 0; }
		struct v3d_kops ops = { k, k_alloc };
		struct v3d_kresult r;
		int e = v3d_kglue_validate(&a, d, a.bo_handle_count, (const void *)(uintptr_t)a.bin_cl,
		    (const void *)(uintptr_t)a.shader_rec, (const void *)(uintptr_t)a.uniforms, &ops, &r);
		if (e) { k->rejected_submits++; return e == ENOMEM ? kIOReturnNoMemory : kIOReturnBadArgument; }
		k->submits++;
		if (a.bin_cl_size) { k->jobs_with_bin++; k->bin_bytes += a.bin_cl_size; } else k->jobs_render_only++;
		if (a.shader_rec_count) k->jobs_with_recs++;
		k->uniform_bytes += a.uniforms_size;
		a.seqno = ++k->next_seqno;
		k->finished = a.seqno;          /* no GPU: completes at once */
		memcpy(out, &a, sizeof(a));
		return 0;
	}
	default:
		return kIOReturnUnsupported;
	}
}

static int
k_call_scalar(void *ctx, uint32_t sel, const uint64_t *in, uint32_t n_in, uint64_t *out, uint32_t *n_out)
{
	struct kernel *k = ctx;
	(void)out; (void)n_out;
	if (sel == kV3DMethodCloseBO) {
		if (n_in != 1 || in[0] == 0 || in[0] >= KMAX) return kIOReturnBadArgument;
		struct kbo *b = &k->bo[in[0] - 1];
		if (!b->used) return kIOReturnNotFound;
		free(b->mem);
		v3d_kglue_free_shader(b->shader);
		memset(b, 0, sizeof(*b));
		return 0;
	}
	return kIOReturnUnsupported;
}

static int
k_map(void *ctx, uint32_t handle, uint64_t *addr, uint64_t *size)
{
	struct kernel *k = ctx;
	if (!handle || handle >= KMAX || !k->bo[handle - 1].used) return kIOReturnNotFound;
	if (k->bo[handle - 1].shader) return kIOReturnNotPermitted;     /* shaders are immutable */
	*addr = (uint64_t)(uintptr_t)k->bo[handle - 1].mem;
	*size = k->bo[handle - 1].size;
	k->open_maps++;
	return 0;
}

static int
k_unmap(void *ctx, uint32_t handle, uint64_t addr)
{
	struct kernel *k = ctx;
	(void)handle; (void)addr;
	k->open_maps--;
	return 0;
}

static void k_close(void *ctx) { (void)ctx; }


static inline struct v3d_transport
mock_transport(struct kernel *k)
{
	struct v3d_transport t = { k, k_call_struct, k_call_scalar, k_map, k_unmap, k_close };
	return t;
}

#endif
