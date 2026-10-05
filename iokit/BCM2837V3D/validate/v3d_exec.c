/*
 * Validated-submit driver for the vendored VC4 validators.
 *
 * This is the V3D kext's replacement for Mesa's kernel/vc4_gem.c and for
 * Linux's vc4_get_bcl()/vc4_cl_validate() (drivers/gpu/drm/vc4/vc4_gem.c),
 * which it follows. Differences, all deliberate:
 *   - the caller has already copied the client's data into kernel memory
 *     and passes plain pointers; nothing here touches user memory;
 *   - every size is range-checked and every failure returns an error
 *     (Mesa's mirror falls out of its overflow check with ret == 0);
 *   - the buffers created here (exec, tile, render-list BOs) come from
 *     drm_gem_cma_create() supplied by the caller and are linked on
 *     exec->unref_list for it to copy out and release.
 */
#include "vc4_drv.h"
#include "vc4_packet.h"
#include "v3d_exec.h"

/* Upper bounds, so that a client cannot ask for enormous allocations. The
 * exec BO (bin CL + shader recs + uniforms) must also fit one BO. */
#define V3D_MAX_EXEC_BYTES   (16u * 1024 * 1024)
#define V3D_MAX_SHADER_RECS  (64u * 1024)

static int
v3d_get_bcl(struct drm_device *dev, struct vc4_exec_info *exec,
            const void *bin_cl, const void *shader_rec, const void *uniforms)
{
	struct drm_vc4_submit_cl *args = exec->args;
	uint32_t bin_offset = 0;
	uint64_t shader_rec_offset = roundup((uint64_t)args->bin_cl_size, 16);
	uint64_t uniforms_offset = shader_rec_offset + args->shader_rec_size;
	uint64_t exec_size = uniforms_offset + args->uniforms_size;
	int ret;

	if (args->bin_cl_size > V3D_MAX_EXEC_BYTES ||
	    args->shader_rec_size > V3D_MAX_EXEC_BYTES ||
	    args->uniforms_size > V3D_MAX_EXEC_BYTES ||
	    exec_size > V3D_MAX_EXEC_BYTES ||
	    args->shader_rec_count > V3D_MAX_SHADER_RECS) {
		DRM_ERROR("overflow in exec arguments\n");
		return -EINVAL;
	}

	exec->shader_state = kcalloc(args->shader_rec_count ? args->shader_rec_count : 1,
	                             sizeof(*exec->shader_state), GFP_KERNEL);
	if (exec->shader_state == NULL) {
		return -ENOMEM;
	}
	exec->shader_state_size = args->shader_rec_count;

	exec->exec_bo = drm_gem_cma_create(dev, (size_t)exec_size);
	if (exec->exec_bo == NULL) {
		DRM_ERROR("Couldn't allocate BO for exec\n");
		return -ENOMEM;
	}
	list_addtail(&to_vc4_bo(&exec->exec_bo->base)->unref_head, &exec->unref_list);

	exec->ct0ca = exec->exec_bo->paddr + bin_offset;

	exec->bin_u = (void *)bin_cl;

	exec->shader_rec_u = (void *)shader_rec;
	exec->shader_rec_v = (char *)exec->exec_bo->vaddr + shader_rec_offset;
	exec->shader_rec_p = exec->exec_bo->paddr + (uint32_t)shader_rec_offset;
	exec->shader_rec_size = args->shader_rec_size;

	exec->uniforms_u = (void *)uniforms;
	exec->uniforms_v = (char *)exec->exec_bo->vaddr + uniforms_offset;
	exec->uniforms_p = exec->exec_bo->paddr + (uint32_t)uniforms_offset;
	exec->uniforms_size = args->uniforms_size;

	ret = vc4_validate_bin_cl(dev, (char *)exec->exec_bo->vaddr + bin_offset,
	                          exec->bin_u, exec);
	if (ret) {
		return ret;
	}
	return vc4_validate_shader_recs(dev, exec);
}

int
v3d_exec_validate(struct drm_device *dev, struct vc4_exec_info *exec,
                  const void *bin_cl, const void *shader_rec, const void *uniforms)
{
	struct drm_vc4_submit_cl *args = exec->args;
	int ret;

	/* 4x multisample halves the tile size (32x32 pixels instead of 64x64). */
	if (args->color_write.bits & VC4_RENDER_CONFIG_MS_MODE_4X) {
		exec->tile_width = 32;
		exec->tile_height = 32;
	} else {
		exec->tile_width = 64;
		exec->tile_height = 64;
	}

	if (args->bin_cl_size != 0) {
		ret = v3d_get_bcl(dev, exec, bin_cl, shader_rec, uniforms);
		if (ret) {
			return ret;
		}
	} else {
		if (args->shader_rec_size != 0 || args->shader_rec_count != 0 ||
		    args->uniforms_size != 0) {
			return -EINVAL;     // nothing could consume them
		}
		exec->ct0ca = exec->ct0ea = 0;
	}

	return vc4_get_rcl(dev, exec);
}

void
v3d_exec_release(struct vc4_exec_info *exec)
{
	if (exec->shader_state != NULL) {
		kfree(exec->shader_state);
		exec->shader_state = NULL;
	}
}

void
v3d_validated_shader_free(struct vc4_validated_shader_info *info)
{
	if (info == NULL) {
		return;
	}
	kfree(info->uniform_addr_offsets);
	kfree(info->texture_samples);
	kfree(info);
}
