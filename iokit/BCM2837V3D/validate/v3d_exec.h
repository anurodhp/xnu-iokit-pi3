/*
 * Entry points of the validated-submit path (see v3d_exec.c). Used by the
 * BCM2837V3D kext and by the host test harness in tools/v3d_validate_test.
 */
#ifndef V3D_EXEC_H
#define V3D_EXEC_H

#include "vc4_drv.h"

/*
 * Validates a submit and generates everything the GPU will run.
 *
 * `exec` must be zero-initialised by the caller with args, bo and bo_count
 * filled in (and unref_list empty). On success exec->ct0ca/ct0ea (bin list;
 * both 0 when there is none) and ct1ca/ct1ea (render list) hold GPU
 * addresses inside BOs created through drm_gem_cma_create(), which are
 * linked on exec->unref_list; the caller copies each BO's staging `vaddr`
 * into the real GPU memory and releases them after the job. On failure the
 * same list holds whatever was created before the failure, to be released.
 *
 * bin_cl / shader_rec / uniforms are kernel copies of the client's data.
 * Call v3d_exec_release() afterwards in both cases.
 */
int v3d_exec_validate(struct drm_device *dev, struct vc4_exec_info *exec,
                      const void *bin_cl, const void *shader_rec, const void *uniforms);
void v3d_exec_release(struct vc4_exec_info *exec);

/* Shader BO creation: `shader_obj->vaddr` holds the QPU program (size in
 * base.size, a multiple of 8). Returns NULL if it is not provably safe. */
struct vc4_validated_shader_info *vc4_validate_shader(struct drm_gem_cma_object *shader_obj);
void v3d_validated_shader_free(struct vc4_validated_shader_info *info);

#endif /* V3D_EXEC_H */
