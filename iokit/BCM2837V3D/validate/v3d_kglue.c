/*
 * Glue between the BCM2837V3D driver and the vendored validators: supplies
 * drm_gem_cma_create() on top of the driver's allocator callbacks and the
 * narrow API of v3d_kglue.h. Also compiled by the host tests (with a fake
 * allocator) so the glue itself is exercised.
 */
#include <stddef.h>
#include "v3d_kglue.h"
#include "v3d_exec.h"

#ifdef V3D_KERNEL
/* boot-arg v3d-debug=1: print the validators' rejection reasons */
int v3d_kglue_debug;
#endif

struct glue_ctx {
	const struct v3d_kops *ops;
};

static struct drm_gem_cma_object *
glue_create(struct drm_device *dev, size_t size, bool want_staging)
{
	const struct glue_ctx *g = dev->priv;
	struct drm_vc4_bo *b;
	uint32_t gpu = 0;
	void *staging = NULL;

	if (size == 0) {
		return NULL;
	}
	b = kcalloc(1, sizeof(*b), GFP_KERNEL);
	if (b == NULL) {
		return NULL;
	}
	if (g->ops->alloc_bo(g->ops->ctx, size, want_staging, &gpu, &staging) != 0) {
		kfree(b);
		return NULL;
	}
	b->base.base.size = size;
	b->base.base.dev = dev;
	b->base.paddr = gpu;
	b->base.vaddr = staging;
	return &b->base;
}

struct drm_gem_cma_object *
drm_gem_cma_create(struct drm_device *dev, size_t size)
{
	return glue_create(dev, size, true);
}

struct drm_gem_cma_object *
drm_gem_cma_create_nostage(struct drm_device *dev, size_t size)
{
	return glue_create(dev, size, false);
}

int
v3d_kglue_validate(const struct v3d_submit_cl *cargs, const struct v3d_kbo_desc *descs,
    uint32_t nbos, const void *bin_cl, const void *shader_rec, const void *uniforms,
    const struct v3d_kops *ops, struct v3d_kresult *out)
{
	struct glue_ctx g = { ops };
	struct drm_device dev = { &g };
	struct vc4_exec_info *exec;
	struct drm_vc4_bo *wrap = NULL;
	struct drm_gem_cma_object **bo = NULL;
	struct v3d_submit_cl *args;
	struct list_head *node;
	int ret = -ENOMEM;

	exec = kcalloc(1, sizeof(*exec), GFP_KERNEL);
	args = kcalloc(1, sizeof(*args), GFP_KERNEL);
	wrap = kcalloc(nbos ? nbos : 1, sizeof(*wrap), GFP_KERNEL);
	bo = kcalloc(nbos ? nbos : 1, sizeof(*bo), GFP_KERNEL);
	if (exec == NULL || args == NULL || wrap == NULL || bo == NULL) {
		goto out;
	}
	*args = *cargs;     /* the validators must not see later changes */
	for (uint32_t i = 0; i < nbos; i++) {
		wrap[i].base.base.size = descs[i].size;
		wrap[i].base.base.dev = &dev;
		wrap[i].base.paddr = descs[i].gpu_addr;
		wrap[i].base.vaddr = NULL;     /* the validators never read client BOs */
		wrap[i].validated_shader = descs[i].shader;
		bo[i] = &wrap[i].base;
	}
	exec->args = args;
	exec->bo = bo;
	exec->bo_count = nbos;

	ret = v3d_exec_validate(&dev, exec, bin_cl, shader_rec, uniforms);   /* 0 or -errno */
	ret = ret < 0 ? -ret : ret;
	if (ret == 0) {
		out->ct0ca = exec->ct0ca;
		out->ct0ea = exec->ct0ea;
		out->ct1ca = exec->ct1ca;
		out->ct1ea = exec->ct1ea;
	}

	/* Free the bookkeeping for the BOs created along the way (the driver
	 * owns the memory behind them). */
	node = exec->unref_list.next;
	while (node != NULL) {
		struct list_head *next = node->next;
		kfree((char *)node - offsetof(struct drm_vc4_bo, unref_head));
		node = next;
	}
	v3d_exec_release(exec);
out:
	kfree(exec);
	kfree(args);
	kfree(wrap);
	kfree(bo);
	return ret;
}

struct vc4_validated_shader_info *
v3d_kglue_validate_shader(const void *code, uint32_t size)
{
	struct drm_gem_cma_object obj;

	if (size == 0 || (size & 7) != 0) {
		return NULL;
	}
	memset(&obj, 0, sizeof(obj));
	obj.base.size = size;
	obj.vaddr = (void *)code;
	return vc4_validate_shader(&obj);
}

void
v3d_kglue_free_shader(struct vc4_validated_shader_info *info)
{
	v3d_validated_shader_free(info);
}
