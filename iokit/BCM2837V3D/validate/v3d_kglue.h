/*
 * Narrow C interface between the BCM2837V3D C++ driver and the vendored C
 * validators. The validator headers define macros (max, min, roundup, BIT,
 * assert, kmalloc...) that must not leak into C++ translation units that
 * include IOKit headers, so the driver only ever sees this header.
 *
 * The allocation shims the validators use (v3d_kalloc, v3d_kfree,
 * v3d_krealloc_nosize, v3d_bug_on) are declared in vc4_simulator_validate.h
 * and defined by the C++ driver.
 */
#ifndef V3D_KGLUE_H
#define V3D_KGLUE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "BCM2837V3DUAPI.h"

#ifdef __cplusplus
extern "C" {
#endif

struct vc4_validated_shader_info;

/* What the validators need to know about one BO the client named. */
struct v3d_kbo_desc {
	uint32_t size;                                 /* usable bytes */
	uint32_t gpu_addr;                             /* bus address */
	struct vc4_validated_shader_info *shader;      /* non-NULL: validated shader BO */
};

/*
 * Buffers the validators create for a job (exec, tile, render list) come
 * from the driver. `*staging` receives ordinary cacheable memory the
 * validators write through (NULL when want_staging is false); the driver
 * copies it into the BO after a successful validation. Returns 0 or an
 * errno value.
 */
struct v3d_kops {
	void *ctx;
	int (*alloc_bo)(void *ctx, size_t size, bool want_staging, uint32_t *gpu_addr, void **staging);
};

struct v3d_kresult {
	uint32_t ct0ca, ct0ea;      /* binner list (equal when there is none) */
	uint32_t ct1ca, ct1ea;      /* render list */
};

/* 0 or an errno value. `args` is the client's v3d_submit_cl (already in
 * kernel memory), the three data pointers are kernel copies. */
int v3d_kglue_validate(const struct v3d_submit_cl *args, const struct v3d_kbo_desc *bos,
    uint32_t nbos, const void *bin_cl, const void *shader_rec, const void *uniforms,
    const struct v3d_kops *ops, struct v3d_kresult *out);

/* `code` is `size` bytes (multiple of 8) of QPU program, in kernel memory.
 * Returns the validated-shader record or NULL if the program is refused. */
struct vc4_validated_shader_info *v3d_kglue_validate_shader(const void *code, uint32_t size);
void v3d_kglue_free_shader(struct vc4_validated_shader_info *info);

#ifdef __cplusplus
}
#endif
#endif
