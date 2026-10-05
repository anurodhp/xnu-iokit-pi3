/*
 * Layout check: the BCM2837V3D user-client structs must be byte-identical to
 * the DRM vc4 uapi structs Mesa's driver fills in (the libdrm shim passes
 * them through without conversion). Build: see run.sh.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "BCM2837V3DUAPI.h"
#include "drm-uapi/vc4_drm.h"

static int bad;
#define SAME(T, D, f) do { if (offsetof(T, f) != offsetof(D, f)) { printf("MISMATCH %s.%s: %zu vs %zu\n", #T, #f, offsetof(T, f), offsetof(D, f)); bad = 1; } } while (0)
#define SIZE(T, D) do { if (sizeof(T) != sizeof(D)) { printf("MISMATCH sizeof(%s)=%zu sizeof(%s)=%zu\n", #T, sizeof(T), #D, sizeof(D)); bad = 1; } } while (0)

int main(void)
{
	SIZE(struct v3d_get_param, struct drm_vc4_get_param);
	SAME(struct v3d_get_param, struct drm_vc4_get_param, param);
	SAME(struct v3d_get_param, struct drm_vc4_get_param, value);
	SIZE(struct v3d_create_bo, struct drm_vc4_create_bo);
	SAME(struct v3d_create_bo, struct drm_vc4_create_bo, size);
	SAME(struct v3d_create_bo, struct drm_vc4_create_bo, flags);
	SAME(struct v3d_create_bo, struct drm_vc4_create_bo, handle);
	SIZE(struct v3d_wait_seqno, struct drm_vc4_wait_seqno);
	SAME(struct v3d_wait_seqno, struct drm_vc4_wait_seqno, seqno);
	SAME(struct v3d_wait_seqno, struct drm_vc4_wait_seqno, timeout_ns);
	SIZE(struct v3d_wait_bo, struct drm_vc4_wait_bo);
	SAME(struct v3d_wait_bo, struct drm_vc4_wait_bo, handle);
	SAME(struct v3d_wait_bo, struct drm_vc4_wait_bo, timeout_ns);
	SIZE(struct v3d_create_shader_bo, struct drm_vc4_create_shader_bo);
	SAME(struct v3d_create_shader_bo, struct drm_vc4_create_shader_bo, size);
	SAME(struct v3d_create_shader_bo, struct drm_vc4_create_shader_bo, flags);
	SAME(struct v3d_create_shader_bo, struct drm_vc4_create_shader_bo, data);
	SAME(struct v3d_create_shader_bo, struct drm_vc4_create_shader_bo, handle);
	SIZE(struct v3d_submit_rcl_surface, struct drm_vc4_submit_rcl_surface);
	SAME(struct v3d_submit_rcl_surface, struct drm_vc4_submit_rcl_surface, hindex);
	SAME(struct v3d_submit_rcl_surface, struct drm_vc4_submit_rcl_surface, offset);
	SAME(struct v3d_submit_rcl_surface, struct drm_vc4_submit_rcl_surface, bits);
	SAME(struct v3d_submit_rcl_surface, struct drm_vc4_submit_rcl_surface, flags);
	SIZE(struct v3d_submit_cl, struct drm_vc4_submit_cl);
	SAME(struct v3d_submit_cl, struct drm_vc4_submit_cl, bin_cl);
	SAME(struct v3d_submit_cl, struct drm_vc4_submit_cl, shader_rec);
	SAME(struct v3d_submit_cl, struct drm_vc4_submit_cl, uniforms);
	SAME(struct v3d_submit_cl, struct drm_vc4_submit_cl, bo_handles);
	SAME(struct v3d_submit_cl, struct drm_vc4_submit_cl, bin_cl_size);
	SAME(struct v3d_submit_cl, struct drm_vc4_submit_cl, shader_rec_size);
	SAME(struct v3d_submit_cl, struct drm_vc4_submit_cl, shader_rec_count);
	SAME(struct v3d_submit_cl, struct drm_vc4_submit_cl, uniforms_size);
	SAME(struct v3d_submit_cl, struct drm_vc4_submit_cl, bo_handle_count);
	SAME(struct v3d_submit_cl, struct drm_vc4_submit_cl, width);
	SAME(struct v3d_submit_cl, struct drm_vc4_submit_cl, height);
	SAME(struct v3d_submit_cl, struct drm_vc4_submit_cl, min_x_tile);
	SAME(struct v3d_submit_cl, struct drm_vc4_submit_cl, max_y_tile);
	SAME(struct v3d_submit_cl, struct drm_vc4_submit_cl, color_read);
	SAME(struct v3d_submit_cl, struct drm_vc4_submit_cl, msaa_zs_write);
	SAME(struct v3d_submit_cl, struct drm_vc4_submit_cl, clear_color);
	SAME(struct v3d_submit_cl, struct drm_vc4_submit_cl, clear_z);
	SAME(struct v3d_submit_cl, struct drm_vc4_submit_cl, clear_s);
	SAME(struct v3d_submit_cl, struct drm_vc4_submit_cl, flags);
	SAME(struct v3d_submit_cl, struct drm_vc4_submit_cl, seqno);
	SAME(struct v3d_submit_cl, struct drm_vc4_submit_cl, perfmonid);
	SAME(struct v3d_submit_cl, struct drm_vc4_submit_cl, in_sync);
	SAME(struct v3d_submit_cl, struct drm_vc4_submit_cl, out_sync);
	printf(bad ? "ABI MISMATCH\n" : "ABI OK\n");
	return bad;
}
