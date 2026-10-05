/*
 * Host test of the libdrm shim (tools/userland_staging/v3d_drm) against a
 * mock kernel. The mock implements the BCM2837V3D user-client methods with
 * the same argument checks as BCM2837V3DUserClient::externalMethod (exact
 * struct sizes, handle table, immutable shader BOs) and runs submissions
 * through the real validators via v3d_kglue.h, so what is exercised end to
 * end is: Mesa-style ioctl structs -> shim -> kernel ABI -> validators.
 * Jobs complete instantly (there is no GPU model).
 */
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
#include "drm-uapi/vc4_drm.h"      /* Mesa's copy of the DRM uapi: what the driver really passes */

/* shim allocation hooks the validators expect from the driver */
void v3d_bug_on(const char *f, int l) { fprintf(stderr, "BUG_ON %s:%d\n", f, l); abort(); }

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
		if (!si) return kIOReturnNotPermitted;
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
		struct v3d_kops ops = { k, k_alloc };
		struct v3d_kresult r;
		int e = v3d_kglue_validate(&a, d, a.bo_handle_count, (const void *)(uintptr_t)a.bin_cl,
		    (const void *)(uintptr_t)a.shader_rec, (const void *)(uintptr_t)a.uniforms, &ops, &r);
		if (e) return e == ENOMEM ? kIOReturnNoMemory : kIOReturnBadArgument;
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

/* ------------------------------- tests ------------------------------- */

static int run_n, fail_n;
#define EXPECT(c, ...) do { run_n++; if (!(c)) { fail_n++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define EXPECT_ERR(call, err) do { errno = 0; int _r = (call); EXPECT(_r == -1 && errno == (err), "%s: ret %d errno %d (want -1/%d)", #call, _r, errno, (err)); } while (0)

/* QPU encodings, as in test_validate.c */
#include "vc4_qpu_defines.h"
#include "vc4_packet.h"
static uint64_t qpu_nop(uint32_t sig)
{
	return QPU_SET_FIELD(sig, QPU_SIG) | QPU_SET_FIELD(QPU_W_NOP, QPU_WADDR_ADD) |
	    QPU_SET_FIELD(QPU_W_NOP, QPU_WADDR_MUL) | QPU_SET_FIELD(QPU_COND_ALWAYS, QPU_COND_ADD) |
	    QPU_SET_FIELD(QPU_COND_ALWAYS, QPU_COND_MUL) | QPU_SET_FIELD(QPU_R_NOP, QPU_RADDR_A) |
	    QPU_SET_FIELD(QPU_R_NOP, QPU_RADDR_B);
}

static uint32_t
create_bo(int fd, uint32_t size)
{
	struct drm_vc4_create_bo c = { .size = size };
	return drmIoctl(fd, DRM_IOCTL_VC4_CREATE_BO, &c) == 0 ? c.handle : 0;
}

struct buf { uint8_t d[4096]; uint32_t n; };
static void p8(struct buf *b, uint32_t v) { b->d[b->n++] = (uint8_t)v; }
static void p32(struct buf *b, uint32_t v) { memcpy(&b->d[b->n], &v, 4); b->n += 4; }

int
main(void)
{
	struct kernel *k = calloc(1, sizeof(*k));
	k->next_gpu = 0xC0100000u;
	struct v3d_transport t = { k, k_call_struct, k_call_scalar, k_map, k_unmap, k_close };
	int fd = v3d_drm_open_transport(&t);
	EXPECT(fd >= 0, "open");

	/* errno table vs the SDK's IOReturn.h */
	struct { int io; int err; } tbl[] = {
		{ kIOReturnError, EIO }, { kIOReturnNoMemory, ENOMEM }, { kIOReturnNoResources, ENOMEM },
		{ kIOReturnNoDevice, ENODEV }, { kIOReturnNotPrivileged, EACCES }, { kIOReturnBadArgument, EINVAL },
		{ kIOReturnExclusiveAccess, EBUSY }, { kIOReturnUnsupported, ENOSYS }, { kIOReturnVMError, EFAULT },
		{ kIOReturnBusy, EBUSY }, { kIOReturnTimeout, ETIME }, { kIOReturnNotReady, EBUSY },
		{ kIOReturnNotAttached, ENXIO }, { kIOReturnNoSpace, ENOSPC }, { kIOReturnNotPermitted, EPERM },
		{ kIOReturnAborted, EINTR }, { kIOReturnNotFound, ENOENT }, { 0, 0 },
	};
	for (unsigned i = 0; i < sizeof(tbl) / sizeof(tbl[0]); i++) {
		EXPECT(v3d_drm_ioreturn_to_errno(tbl[i].io) == tbl[i].err, "errno mapping for IOReturn %#x", tbl[i].io);
	}

	/* ioctl numbers the shim switches on match Mesa's headers */
	EXPECT((DRM_IOCTL_VC4_SUBMIT_CL & 0xff) == 0x40, "submit nr");
	EXPECT((DRM_IOCTL_VC4_WAIT_SEQNO & 0xff) == 0x41, "wait_seqno nr");
	EXPECT((DRM_IOCTL_VC4_CREATE_BO & 0xff) == 0x43, "create_bo nr");
	EXPECT((DRM_IOCTL_VC4_MMAP_BO & 0xff) == 0x44, "mmap_bo nr");
	EXPECT((DRM_IOCTL_VC4_CREATE_SHADER_BO & 0xff) == 0x45, "shader nr");
	EXPECT((DRM_IOCTL_VC4_GET_PARAM & 0xff) == 0x47, "get_param nr");
	EXPECT((DRM_IOCTL_VC4_GEM_MADVISE & 0xff) == 0x4b, "madvise nr");
	EXPECT((DRM_IOCTL_GEM_CLOSE & 0xff) == 0x09 && ((DRM_IOCTL_GEM_CLOSE >> 8) & 0xff) == 'd', "gem_close");

	/* params */
	struct drm_vc4_get_param gp = { .param = DRM_VC4_PARAM_V3D_IDENT0 };
	EXPECT(drmIoctl(fd, DRM_IOCTL_VC4_GET_PARAM, &gp) == 0 && gp.value == 0x02443356, "ident0 %llx", (unsigned long long)gp.value);
	gp.param = DRM_VC4_PARAM_SUPPORTS_BRANCHES;
	EXPECT(drmIoctl(fd, DRM_IOCTL_VC4_GET_PARAM, &gp) == 0 && gp.value == 1, "branches");
	gp.param = 99;
	EXPECT_ERR(drmIoctl(fd, DRM_IOCTL_VC4_GET_PARAM, &gp), EINVAL);

	/* BOs and mappings */
	uint32_t bo = create_bo(fd, 5000);
	EXPECT(bo != 0, "create_bo");
	struct drm_vc4_mmap_bo mm = { .handle = bo };
	EXPECT(drmIoctl(fd, DRM_IOCTL_VC4_MMAP_BO, &mm) == 0 && (mm.offset & 0xfff) == 0, "mmap_bo token");
	uint8_t *p = v3d_drm_mmap(fd, 5000, mm.offset);
	EXPECT(p != (void *)-1 && p != NULL, "v3d_drm_mmap");
	if (p != (void *)-1) {
		p[0] = 0x5a; p[4999] = 0xa5;
		uint8_t *p2 = v3d_drm_mmap(fd, 5000, mm.offset);
		EXPECT(p2 != (void *)-1 && p2[0] == 0x5a && p2[4999] == 0xa5, "second mapping sees the data");
		EXPECT(v3d_drm_munmap(fd, p2, 5000) == 0, "munmap");
		EXPECT(v3d_drm_munmap(fd, p, 5000) == 0, "munmap 2");
		EXPECT_ERR(v3d_drm_munmap(fd, p, 5000), EINVAL);
	}
	errno = 0;
	EXPECT(v3d_drm_mmap(fd, 5000, (uint64_t)77 << 12) == (void *)-1 && errno == ENOENT, "mmap unknown handle");
	errno = 0;
	EXPECT(v3d_drm_mmap(fd, 1 << 20, mm.offset) == (void *)-1 && errno == EINVAL, "mmap longer than the BO");
	mm.handle = 4000;
	EXPECT_ERR(drmIoctl(fd, DRM_IOCTL_VC4_MMAP_BO, &mm), ENOENT);

	/* tiling / madvise / label / perfmon */
	struct drm_vc4_set_tiling st = { .handle = bo, .modifier = 0x0700000000000001ull };
	struct drm_vc4_get_tiling gt = { .handle = bo };
	EXPECT(drmIoctl(fd, DRM_IOCTL_VC4_SET_TILING, &st) == 0 && drmIoctl(fd, DRM_IOCTL_VC4_GET_TILING, &gt) == 0 &&
	    gt.modifier == st.modifier, "tiling round trip");
	struct drm_vc4_gem_madvise mad = { .handle = bo, .madv = VC4_MADV_DONTNEED };
	EXPECT(drmIoctl(fd, DRM_IOCTL_VC4_GEM_MADVISE, &mad) == 0 && mad.retained == 1, "madvise");
	struct drm_vc4_label_bo lb = { .handle = bo };
	EXPECT(drmIoctl(fd, DRM_IOCTL_VC4_LABEL_BO, &lb) == 0, "label");
	struct drm_vc4_perfmon_create pc = { 0 };
	EXPECT_ERR(drmIoctl(fd, DRM_IOCTL_VC4_PERFMON_CREATE, &pc), ENOSYS);
	struct drm_vc4_get_hang_state hs = { 0 };
	EXPECT_ERR(drmIoctl(fd, DRM_IOCTL_VC4_GET_HANG_STATE, &hs), ENOENT);
	uint64_t cap = 5;
	EXPECT(drmGetCap(fd, DRM_CAP_SYNCOBJ, &cap) == 0 && cap == 0, "syncobj cap off");

	/* generic error paths */
	EXPECT_ERR(drmIoctl(fd + 100, DRM_IOCTL_VC4_GET_PARAM, &gp), EBADF);
	EXPECT_ERR(drmIoctl(fd, 0x1234, &gp), ENOTTY);
	EXPECT_ERR(drmIoctl(fd, (('d' << 8) | 0x7f), &gp), ENOTTY);

	/* close */
	struct drm_gem_close gc = { .handle = bo };
	EXPECT(drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &gc) == 0, "gem_close");
	EXPECT_ERR(drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &gc), ENOENT);
	struct drm_vc4_wait_bo wb = { .handle = bo, .timeout_ns = 1 };
	EXPECT_ERR(drmIoctl(fd, DRM_IOCTL_VC4_WAIT_BO, &wb), ENOENT);

	/* shader BOs: a safe program is accepted, an unsafe one refused */
	uint64_t code[4] = { qpu_nop(QPU_SIG_NONE), qpu_nop(QPU_SIG_PROG_END), qpu_nop(QPU_SIG_NONE), qpu_nop(QPU_SIG_NONE) };
	struct drm_vc4_create_shader_bo sb = { .size = sizeof(code), .data = (uintptr_t)code };
	EXPECT(drmIoctl(fd, DRM_IOCTL_VC4_CREATE_SHADER_BO, &sb) == 0 && sb.handle != 0, "create shader bo");
	uint32_t shader = sb.handle;
	uint64_t bad[4] = { qpu_nop(QPU_SIG_NONE), qpu_nop(QPU_SIG_PROG_END), qpu_nop(QPU_SIG_NONE), qpu_nop(QPU_SIG_NONE) };
	bad[0] = (bad[0] & ~QPU_WADDR_ADD_MASK) | QPU_SET_FIELD(QPU_W_VPM_ADDR, QPU_WADDR_ADD) |
	    QPU_SET_FIELD(QPU_A_OR, QPU_OP_ADD) | QPU_SET_FIELD(QPU_MUX_R0, QPU_ADD_A) | QPU_SET_FIELD(QPU_MUX_R0, QPU_ADD_B);
	sb.data = (uintptr_t)bad;
	EXPECT_ERR(drmIoctl(fd, DRM_IOCTL_VC4_CREATE_SHADER_BO, &sb), EPERM);
	mm.handle = shader;
	EXPECT(drmIoctl(fd, DRM_IOCTL_VC4_MMAP_BO, &mm) == 0, "mmap token for shader");
	errno = 0;
	EXPECT(v3d_drm_mmap(fd, 32, mm.offset) == (void *)-1 && errno == EPERM, "shader BO must not be mappable");

	/* a complete submission, Mesa-struct in, kernel validator behind it */
	uint32_t vbo = create_bo(fd, 4096), color = create_bo(fd, 64 * 64 * 4);
	uint32_t handles[3] = { shader, vbo, color };
	struct buf bin = { .n = 0 }, rec = { .n = 0 };
	p8(&bin, VC4_PACKET_TILE_BINNING_MODE_CONFIG); p32(&bin, 0); p32(&bin, 0); p32(&bin, 0); p8(&bin, 1); p8(&bin, 1); p8(&bin, 0);
	p8(&bin, VC4_PACKET_START_TILE_BINNING);
	p8(&bin, VC4_PACKET_PRIMITIVE_LIST_FORMAT); p8(&bin, VC4_PRIMITIVE_LIST_FORMAT_32_XY | VC4_PRIMITIVE_LIST_FORMAT_TYPE_TRIANGLES);
	p8(&bin, VC4_PACKET_GL_SHADER_STATE); p32(&bin, 1);
	p8(&bin, VC4_PACKET_GL_ARRAY_PRIMITIVE); p8(&bin, 2); p32(&bin, 3); p32(&bin, 0);
	p8(&bin, VC4_PACKET_INCREMENT_SEMAPHORE); p8(&bin, VC4_PACKET_FLUSH);
	/* shader rec: handle indices fs vs cs (0 = the shader) then one attribute (1 = vbo) */
	p32(&rec, 0); p32(&rec, 0); p32(&rec, 0); p32(&rec, 1);
	uint32_t start = rec.n;
	memset(&rec.d[rec.n], 0, 44); rec.n += 44;
	rec.d[start] = 1;                        /* FS single thread */
	rec.d[start + 36 + 4] = 15; rec.d[start + 36 + 5] = 16;

	struct drm_vc4_submit_cl sub;
	memset(&sub, 0, sizeof(sub));
	sub.bin_cl = (uintptr_t)bin.d; sub.bin_cl_size = bin.n;
	sub.shader_rec = (uintptr_t)rec.d; sub.shader_rec_size = rec.n; sub.shader_rec_count = 1;
	sub.bo_handles = (uintptr_t)handles; sub.bo_handle_count = 3;
	sub.width = sub.height = 64;
	sub.color_write.hindex = 2;
	sub.color_write.bits = 1 << 2;
	sub.color_read.hindex = sub.zs_read.hindex = sub.zs_write.hindex = ~0u;
	sub.msaa_color_write.hindex = sub.msaa_zs_write.hindex = ~0u;
	EXPECT(drmIoctl(fd, DRM_IOCTL_VC4_SUBMIT_CL, &sub) == 0, "submit_cl: errno %d", errno);
	EXPECT(sub.seqno == 1, "seqno %llu", (unsigned long long)sub.seqno);
	struct drm_vc4_wait_seqno ws = { .seqno = sub.seqno, .timeout_ns = 1000000000ull };
	EXPECT(drmIoctl(fd, DRM_IOCTL_VC4_WAIT_SEQNO, &ws) == 0, "wait_seqno");
	ws.seqno = 99;
	EXPECT_ERR(drmIoctl(fd, DRM_IOCTL_VC4_WAIT_SEQNO, &ws), ETIME);

	/* using the shader as a vertex buffer is refused */
	handles[1] = shader;
	EXPECT_ERR(drmIoctl(fd, DRM_IOCTL_VC4_SUBMIT_CL, &sub), EINVAL);
	handles[1] = vbo;
	/* an unknown handle in the list */
	handles[2] = 900;
	EXPECT_ERR(drmIoctl(fd, DRM_IOCTL_VC4_SUBMIT_CL, &sub), ENOENT);

	EXPECT(v3d_drm_close(fd) == 0, "close");
	EXPECT(k->open_maps == 0, "leaked kernel mappings: %d", k->open_maps);
	EXPECT_ERR(drmIoctl(fd, DRM_IOCTL_VC4_GET_PARAM, &gp), EBADF);

	printf("%d checks, %d failed\n", run_n, fail_n);
	return fail_n != 0;
}
