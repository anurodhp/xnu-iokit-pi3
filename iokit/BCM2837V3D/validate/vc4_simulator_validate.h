/*
 * Compatibility layer for the vendored VC4 command-list/shader validators.
 *
 * The validator sources next to this file (vc4_validate.c,
 * vc4_validate_shaders.c, vc4_render_cl.c, vc4_packet.h, vc4_drv.h,
 * vc4_qpu_defines.h) come from Mesa 25.2.8,
 * src/gallium/drivers/vc4/{kernel/,vc4_qpu_defines.h}, MIT licensed
 * (Copyright 2014-2015 Broadcom), which mirror Linux's
 * drivers/gpu/drm/vc4 validators for userspace. See README for the
 * modifications made. This header replaces Mesa's own
 * vc4_simulator_validate.h (which drags in Mesa's context headers) so the
 * same sources build in two places:
 *
 *   - V3D_KERNEL defined: inside the BCM2837V3D kext. Allocation goes to
 *     v3d_kalloc()/v3d_kfree() (provided by the kext), diagnostics are
 *     compiled out (a client must not be able to flood the kernel log).
 *   - otherwise: a normal host build (tools/v3d_validate_test), with libc
 *     allocation and stderr diagnostics, for unit tests and fuzzing.
 *
 * BO memory model. The validators write the validated command lists and
 * relocated records through `vaddr` and publish `paddr` (the GPU bus
 * address) into them. The callers give each created/looked-up BO a
 * `vaddr` that points at ordinary cacheable staging memory, never directly
 * at the write-combined GPU memory: the validators emit unaligned 32-bit
 * stores (packet fields at byte offsets 1, 5, 9...), which are not safe on
 * every ARM memory type, and the finished image is copied into the BO in
 * one aligned pass afterwards (see v3d_exec.c).
 */
#ifndef VC4_SIMULATOR_VALIDATE_H
#define VC4_SIMULATOR_VALIDATE_H

/* Inside the kernel (the kext is built with -DKERNEL) this is V3D_KERNEL;
 * the host tests build without it. */
#if defined(KERNEL) && !defined(V3D_KERNEL)
#define V3D_KERNEL 1
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#ifdef V3D_KERNEL
#include <sys/errno.h>
#else
#include <errno.h>
#endif

#include "BCM2837V3DUAPI.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

/* The validators were written against the DRM uapi struct names. */
#define drm_vc4_submit_cl v3d_submit_cl
#define drm_vc4_submit_rcl_surface v3d_submit_rcl_surface

#ifndef VC4_SUBMIT_RCL_SURFACE_READ_IS_FULL_RES
#define VC4_SUBMIT_RCL_SURFACE_READ_IS_FULL_RES V3D_SUBMIT_RCL_SURFACE_READ_IS_FULL_RES
#endif
#ifndef VC4_SUBMIT_CL_USE_CLEAR_COLOR
#define VC4_SUBMIT_CL_USE_CLEAR_COLOR V3D_SUBMIT_CL_USE_CLEAR_COLOR
#endif
#ifndef VC4_SUBMIT_CL_FIXED_RCL_ORDER
#define VC4_SUBMIT_CL_FIXED_RCL_ORDER V3D_SUBMIT_CL_FIXED_RCL_ORDER
#endif
#ifndef VC4_SUBMIT_CL_RCL_ORDER_INCREASING_X
#define VC4_SUBMIT_CL_RCL_ORDER_INCREASING_X V3D_SUBMIT_CL_RCL_ORDER_INCREASING_X
#endif
#ifndef VC4_SUBMIT_CL_RCL_ORDER_INCREASING_Y
#define VC4_SUBMIT_CL_RCL_ORDER_INCREASING_Y V3D_SUBMIT_CL_RCL_ORDER_INCREASING_Y
#endif

#define GFP_KERNEL 0

#ifdef V3D_KERNEL
void *v3d_kalloc(size_t size);               /* zero-filled */
void *v3d_krealloc(void *p, size_t old_size, size_t new_size);
void v3d_kfree(void *p);
/* The validators' rejection messages, printed only with the boot-arg
 * v3d-debug=1 (BCM2837V3D::start sets v3d_kglue_debug): user-triggerable,
 * so off by default, as Linux's DRM_DEBUG is. */
extern int v3d_kglue_debug;
int printf(const char *, ...);
#define DRM_INFO(...) do { if (v3d_kglue_debug) printf("v3d validate: " __VA_ARGS__); } while (0)
#define DRM_ERROR(...) do { if (v3d_kglue_debug) printf("v3d validate: " __VA_ARGS__); } while (0)
#define kmalloc(size, arg) v3d_kalloc(size)
#define kcalloc(count, size, arg) v3d_kalloc((size_t)(count) * (size_t)(size))
#define kfree(ptr) v3d_kfree(ptr)
#else
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#define DRM_INFO(...) fprintf(stderr, __VA_ARGS__)
#define DRM_ERROR(...) fprintf(stderr, __VA_ARGS__)
#define kmalloc(size, arg) malloc(size)
#define kcalloc(count, size, arg) calloc(count, size)
#define kfree(ptr) free(ptr)
#endif

/* Mesa's mirror calls krealloc(ptr, new_size, flags) without the old size;
 * the kernel build tracks sizes in a header (v3d_kalloc), so no old size is
 * needed there either. */
#ifdef V3D_KERNEL
void *v3d_krealloc_nosize(void *p, size_t new_size);
#define krealloc(ptr, size, args) v3d_krealloc_nosize(ptr, size)
#else
#define krealloc(ptr, size, args) realloc(ptr, size)
#endif

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define roundup(x, y) ((((x) + ((y) - 1)) / (y)) * (y))
#define round_up(x, y) roundup(x, y)
#define max(x, y) ((x) > (y) ? (x) : (y))
#define min(x, y) ((x) < (y) ? (x) : (y))
#ifdef V3D_KERNEL
#define assert(c) do { } while (0)
#define BUG_ON(c) do { if (c) { v3d_bug_on(__FILE__, __LINE__); } } while (0)
void v3d_bug_on(const char *file, int line);
#else
#define BUG_ON(condition) assert(!(condition))
#endif
#define BIT(bit) (1u << (bit))

/* Unsigned long-based bitmap interface, as in the Linux kernel. */
#define BITMAP_WORDBITS (sizeof(unsigned long) * 8)
#define BITS_TO_LONGS(bits) (roundup(bits, BITMAP_WORDBITS) / sizeof(unsigned long))

static inline bool
test_bit(unsigned int bit, unsigned long *addr)
{
	return addr[bit / BITMAP_WORDBITS] & (1ul << (bit % BITMAP_WORDBITS));
}

static inline bool
set_bit(unsigned int bit, unsigned long *addr)
{
	return addr[bit / BITMAP_WORDBITS] |= (1ul << (bit % BITMAP_WORDBITS));
}

/* Intrusive list, only used to remember BOs the validator created so the
 * caller can release them. */
struct list_head {
	struct list_head *next;
};

static inline void
list_addtail(struct list_head *node, struct list_head *head)
{
	node->next = NULL;
	while (head->next != NULL) {
		head = head->next;
	}
	head->next = node;
}

struct vc4_exec_info;

struct drm_device {
	void *priv;                       /* the caller's allocator context */
};

struct drm_gem_object {
	size_t size;
	struct drm_device *dev;
};

struct drm_gem_cma_object {
	struct drm_gem_object base;
	uint32_t paddr;                   /* GPU (bus) address of the BO */
	void *vaddr;                      /* staging view, see header comment */
};

struct vc4_validated_shader_info;

struct drm_vc4_bo {
	struct drm_gem_cma_object base;
	struct vc4_validated_shader_info *validated_shader;
	struct list_head unref_head;
};

static inline struct drm_vc4_bo *
to_vc4_bo(struct drm_gem_object *obj)
{
	return (struct drm_vc4_bo *)obj;
}

/* Provided by the caller (kext or test harness): allocate a BO of `size`
 * bytes (zeroed) that is also released by the caller after the job. NULL on
 * failure. */
struct drm_gem_cma_object *drm_gem_cma_create(struct drm_device *dev, size_t size);

/* Same, for a BO the validators never read or write through `vaddr` (the
 * tile state/alloc memory): no staging copy is made, vaddr is NULL, and the
 * BO is simply zero-filled GPU memory. */
struct drm_gem_cma_object *drm_gem_cma_create_nostage(struct drm_device *dev, size_t size);

int vc4_cl_validate(struct drm_device *dev, struct vc4_exec_info *exec);

#endif /* VC4_SIMULATOR_VALIDATE_H */
