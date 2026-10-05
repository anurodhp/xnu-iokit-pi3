/*
 * Host tests for the vendored VC4 validators (iokit/BCM2837V3D/validate).
 *
 * Runs on the build machine, with ASAN+UBSAN (see run.sh). Three layers:
 *   1. hand-built valid submissions the validator must accept, with the
 *      emitted GPU addresses checked against the buffers they should name;
 *   2. hand-built bad submissions it must refuse (one violation each);
 *   3. a seeded fuzzer: random/mutated command lists, shader records,
 *      uniforms and QPU programs. A run may accept or refuse, but must
 *      never crash, and everything it accepts must keep every GPU address
 *      it emitted inside a buffer the job owns (check_outputs()).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "../../iokit/BCM2837V3D/validate/vc4_drv.h"
#include "../../iokit/BCM2837V3D/validate/vc4_packet.h"
#include "../../iokit/BCM2837V3D/validate/vc4_qpu_defines.h"
#include "../../iokit/BCM2837V3D/validate/v3d_exec.h"
#include "../../iokit/BCM2837V3D/validate/v3d_kglue.h"

/* ---------------------------- fake BO world ---------------------------- */

#define MAX_BOS 32
static struct drm_vc4_bo *g_all[256];     /* every BO ever made, for ownership checks */
static int g_all_count;
static uint32_t g_next_paddr = 0xC0100000u;
static struct drm_device g_dev;

static struct drm_vc4_bo *
bo_new(size_t size)
{
	struct drm_vc4_bo *b = calloc(1, sizeof(*b));
	/* Exactly `size` bytes: ASAN then catches any write/read past the BO. */
	b->base.vaddr = calloc(1, size ? size : 1);
	b->base.base.size = size;
	b->base.base.dev = &g_dev;
	b->base.paddr = g_next_paddr;
	g_next_paddr += (uint32_t)((size + 0xfff) & ~0xfffu) + 0x1000;
	g_all[g_all_count++] = b;
	return b;
}

/* The glue (v3d_kglue.c) provides drm_gem_cma_create(); it allocates through
 * these callbacks, which also register the BO so the checks can see it. */
static int
test_alloc_bo(void *ctx, size_t size, bool stage, uint32_t *gpu, void **staging)
{
	(void)ctx;
	struct drm_vc4_bo *b = bo_new(size);
	*gpu = b->base.paddr;
	if (stage) {
		*staging = b->base.vaddr;
	} else {
		free(b->base.vaddr);
		b->base.vaddr = NULL;
		*staging = NULL;
	}
	return 0;
}
static const struct v3d_kops g_ops = { NULL, test_alloc_bo };
struct glue_ctx_layout { const struct v3d_kops *ops; };      /* mirrors v3d_kglue.c */
static struct glue_ctx_layout g_glue_ctx = { &g_ops };

void v3d_bug_on(const char *f, int l) { fprintf(stderr, "BUG_ON %s:%d\n", f, l); abort(); }

static void
world_reset(void)
{
	for (int i = 0; i < g_all_count; i++) {
		free(g_all[i]->base.vaddr);
		v3d_validated_shader_free(g_all[i]->validated_shader);
		free(g_all[i]);
	}
	g_all_count = 0;
	g_next_paddr = 0xC0100000u;
}

static bool
addr_in_some_bo(uint32_t addr, uint32_t len)
{
	for (int i = 0; i < g_all_count; i++) {
		uint32_t lo = g_all[i]->base.paddr, sz = (uint32_t)g_all[i]->base.base.size;
		if (addr >= lo && (uint64_t)addr + len <= (uint64_t)lo + sz) {
			return true;
		}
	}
	return false;
}

/* ---------------------------- QPU helpers ---------------------------- */

static uint64_t
qpu_nop(uint32_t sig)
{
	return QPU_SET_FIELD(sig, QPU_SIG) |
	    QPU_SET_FIELD(QPU_W_NOP, QPU_WADDR_ADD) | QPU_SET_FIELD(QPU_W_NOP, QPU_WADDR_MUL) |
	    QPU_SET_FIELD(QPU_COND_ALWAYS, QPU_COND_ADD) | QPU_SET_FIELD(QPU_COND_ALWAYS, QPU_COND_MUL) |
	    QPU_SET_FIELD(QPU_R_NOP, QPU_RADDR_A) | QPU_SET_FIELD(QPU_R_NOP, QPU_RADDR_B);
}

/* add-ALU write of r0 to `waddr` (an "or" of r0 with itself) */
static uint64_t
qpu_write(uint32_t waddr)
{
	return qpu_nop(QPU_SIG_NONE) | QPU_UPDATE_FIELD(0, waddr, QPU_WADDR_ADD) * 0 |
	    (qpu_nop(QPU_SIG_NONE) & 0) | QPU_SET_FIELD(QPU_A_OR, QPU_OP_ADD) |
	    QPU_SET_FIELD(QPU_MUX_R0, QPU_ADD_A) | QPU_SET_FIELD(QPU_MUX_R0, QPU_ADD_B);
}

static uint64_t
qpu_with_waddr(uint64_t inst, uint32_t waddr)
{
	return QPU_UPDATE_FIELD(inst, waddr, QPU_WADDR_ADD);
}

/* A shader that writes nothing special: nop, end-of-program, two delay slots. */
static size_t
simple_shader(uint64_t *out)
{
	out[0] = qpu_nop(QPU_SIG_NONE);
	out[1] = qpu_nop(QPU_SIG_PROG_END);
	out[2] = qpu_nop(QPU_SIG_NONE);
	out[3] = qpu_nop(QPU_SIG_NONE);
	return 4 * sizeof(uint64_t);
}

static struct drm_vc4_bo *
shader_bo_from(const uint64_t *code, size_t n_inst)
{
	struct drm_vc4_bo *b = bo_new(n_inst * 8);
	memcpy(b->base.vaddr, code, n_inst * 8);
	b->validated_shader = vc4_validate_shader(&b->base);
	return b;
}

/* ---------------------------- CL builders ---------------------------- */

struct buf { uint8_t d[8192]; uint32_t n; };
static void p8(struct buf *b, uint32_t v) { b->d[b->n++] = (uint8_t)v; }
static void p32(struct buf *b, uint32_t v) { memcpy(&b->d[b->n], &v, 4); b->n += 4; }

static void
cl_tile_binning_config(struct buf *b, uint8_t tx, uint8_t ty, uint8_t flags)
{
	p8(b, VC4_PACKET_TILE_BINNING_MODE_CONFIG);
	p32(b, 0); p32(b, 0); p32(b, 0);       /* alloc addr/size, state addr: kernel fills */
	p8(b, tx); p8(b, ty); p8(b, flags);
}
static void cl_start_binning(struct buf *b) { p8(b, VC4_PACKET_START_TILE_BINNING); }
static void cl_prim_list_format(struct buf *b) { p8(b, VC4_PACKET_PRIMITIVE_LIST_FORMAT); p8(b, VC4_PRIMITIVE_LIST_FORMAT_32_XY | VC4_PRIMITIVE_LIST_FORMAT_TYPE_TRIANGLES); }
static void cl_gem_handles(struct buf *b, uint32_t h0, uint32_t h1) { p8(b, VC4_PACKET_GEM_HANDLES); p32(b, h0); p32(b, h1); }
static void cl_shader_state(struct buf *b, uint32_t attrs) { p8(b, VC4_PACKET_GL_SHADER_STATE); p32(b, attrs); }
static void cl_array_prim(struct buf *b, uint32_t length, uint32_t base) { p8(b, VC4_PACKET_GL_ARRAY_PRIMITIVE); p8(b, 2); p32(b, length); p32(b, base); }
static void cl_indexed_prim(struct buf *b, uint32_t type16, uint32_t length, uint32_t off, uint32_t maxidx) { p8(b, VC4_PACKET_GL_INDEXED_PRIMITIVE); p8(b, type16 ? VC4_INDEX_BUFFER_U16 | 2 : 2); p32(b, length); p32(b, off); p32(b, maxidx); }
static void cl_end(struct buf *b) { p8(b, VC4_PACKET_INCREMENT_SEMAPHORE); p8(b, VC4_PACKET_FLUSH); }

/* Shader record for one shader state with `nattr` attributes, all pointing
 * at BO index `vbo`, shaders at fs/vs/cs. Appends handles then the record. */
static void
shader_rec(struct buf *b, uint32_t fs, uint32_t vs, uint32_t cs, uint32_t vbo, uint32_t nattr,
           uint32_t attr_off, uint32_t attr_size_m1, uint32_t stride)
{
	p32(b, fs); p32(b, vs); p32(b, cs);
	for (uint32_t i = 0; i < nattr; i++) {
		p32(b, vbo);
	}
	uint32_t start = b->n;
	memset(&b->d[b->n], 0, 36 + nattr * 8);
	b->d[b->n + 0] = 1;                       /* FS single thread */
	b->n += 36 + nattr * 8;
	for (uint32_t i = 0; i < nattr; i++) {
		uint32_t o = start + 36 + i * 8;
		memcpy(&b->d[o], &attr_off, 4);
		b->d[o + 4] = (uint8_t)attr_size_m1;
		b->d[o + 5] = (uint8_t)stride;
	}
	/* round the record to what gl_shader_rec_size gives; already exact */
}

/* ---------------------------- scenario ---------------------------- */

struct scenario {
	struct buf bin, rec, uni;
	struct v3d_submit_cl args;
	struct drm_gem_cma_object *bos[MAX_BOS];
	uint32_t nbos;
	struct drm_vc4_bo *color, *vbo, *ib, *shader;
};

static void
scenario_basic(struct scenario *s)
{
	memset(s, 0, sizeof(*s));
	uint64_t code[4];
	simple_shader(code);
	s->shader = shader_bo_from(code, 4);
	s->vbo = bo_new(4096);
	s->ib = bo_new(1024);
	s->color = bo_new(64 * 64 * 4);
	s->bos[0] = &s->shader->base;     /* index 0 */
	s->bos[1] = &s->vbo->base;        /* index 1 */
	s->bos[2] = &s->ib->base;         /* index 2 */
	s->bos[3] = &s->color->base;      /* index 3 */
	s->nbos = 4;

	cl_tile_binning_config(&s->bin, 1, 1, 0);
	cl_start_binning(&s->bin);
	cl_prim_list_format(&s->bin);
	cl_gem_handles(&s->bin, 2, 0);
	cl_shader_state(&s->bin, 1);              /* 1 attribute */
	cl_array_prim(&s->bin, 3, 0);
	cl_end(&s->bin);

	shader_rec(&s->rec, 0, 0, 0, 1, 1, 0, 15, 16);

	struct v3d_submit_cl *a = &s->args;
	a->bin_cl_size = s->bin.n;
	a->shader_rec_size = s->rec.n;
	a->shader_rec_count = 1;
	a->uniforms_size = 0;
	a->bo_handle_count = s->nbos;
	a->width = 64; a->height = 64;
	a->min_x_tile = a->min_y_tile = 0; a->max_x_tile = a->max_y_tile = 0;
	a->color_write.hindex = 3;
	a->color_write.bits = (VC4_RENDER_CONFIG_FORMAT_RGBA8888 << VC4_RENDER_CONFIG_FORMAT_SHIFT);
	a->color_read.hindex = a->zs_read.hindex = a->zs_write.hindex = 0xffffffffu;
	a->msaa_color_write.hindex = a->msaa_zs_write.hindex = 0xffffffffu;
}

static int
run(struct scenario *s, struct vc4_exec_info *exec)
{
	memset(exec, 0, sizeof(*exec));
	exec->args = &s->args;
	exec->bo = s->bos;
	exec->bo_count = s->nbos;
	int r = v3d_exec_validate(&g_dev, exec, s->bin.d, s->rec.d, s->uni.d);
	return r;
}

/* Everything the validator wrote that the GPU will dereference must land in
 * a buffer the job owns. Walks the validated bin CL, the shader records and
 * the render CL using the hardware packet sizes. */
static const uint8_t pkt_len[256] = {
	[VC4_PACKET_HALT] = 1, [VC4_PACKET_NOP] = 1, [VC4_PACKET_FLUSH] = 1, [VC4_PACKET_FLUSH_ALL] = 1,
	[VC4_PACKET_START_TILE_BINNING] = 1, [VC4_PACKET_INCREMENT_SEMAPHORE] = 1,
	[VC4_PACKET_WAIT_ON_SEMAPHORE] = 1, [VC4_PACKET_BRANCH_TO_SUB_LIST] = 5,
	[VC4_PACKET_STORE_MS_TILE_BUFFER] = 1, [VC4_PACKET_STORE_MS_TILE_BUFFER_AND_EOF] = 1,
	[VC4_PACKET_STORE_FULL_RES_TILE_BUFFER] = 5, [VC4_PACKET_LOAD_FULL_RES_TILE_BUFFER] = 5,
	[VC4_PACKET_STORE_TILE_BUFFER_GENERAL] = 7, [VC4_PACKET_LOAD_TILE_BUFFER_GENERAL] = 7,
	[VC4_PACKET_GL_INDEXED_PRIMITIVE] = 14, [VC4_PACKET_GL_ARRAY_PRIMITIVE] = 10,
	[VC4_PACKET_PRIMITIVE_LIST_FORMAT] = 2, [VC4_PACKET_GL_SHADER_STATE] = 5,
	[VC4_PACKET_CONFIGURATION_BITS] = 4, [VC4_PACKET_FLAT_SHADE_FLAGS] = 5,
	[VC4_PACKET_POINT_SIZE] = 5, [VC4_PACKET_LINE_WIDTH] = 5, [VC4_PACKET_RHT_X_BOUNDARY] = 3,
	[VC4_PACKET_DEPTH_OFFSET] = 5, [VC4_PACKET_CLIP_WINDOW] = 9, [VC4_PACKET_VIEWPORT_OFFSET] = 5,
	[VC4_PACKET_CLIPPER_XY_SCALING] = 9, [VC4_PACKET_CLIPPER_Z_SCALING] = 9,
	[VC4_PACKET_TILE_BINNING_MODE_CONFIG] = 16, [VC4_PACKET_TILE_RENDERING_MODE_CONFIG] = 11,
	[VC4_PACKET_CLEAR_COLORS] = 14, [VC4_PACKET_TILE_COORDINATES] = 3,
};

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

static int
check_outputs(struct vc4_exec_info *e, const char *what)
{
	int bad = 0;
#define FAIL(...) do { fprintf(stderr, "OUTPUT CHECK FAILED (%s): ", what); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); bad = 1; } while (0)
	/* bin CL, inside exec_bo */
	if (e->ct0ca != e->ct0ea) {
		struct drm_gem_cma_object *x = e->exec_bo;
		if (!x || e->ct0ca < x->paddr || e->ct0ea > x->paddr + x->base.size || e->ct0ea < e->ct0ca) {
			FAIL("bin range %#x-%#x outside exec_bo", e->ct0ca, e->ct0ea);
			return bad;
		}
		const uint8_t *p = (const uint8_t *)x->vaddr + (e->ct0ca - x->paddr);
		uint32_t n = e->ct0ea - e->ct0ca, off = 0;
		while (off < n) {
			uint8_t op = p[off];
			if (op == VC4_PACKET_GEM_HANDLES || pkt_len[op] == 0) {
				FAIL("bin CL has non-hardware packet %u at %u", op, off);
				return 1;
			}
			if (off + pkt_len[op] > n) {
				FAIL("bin CL packet overruns at %u", off);
				return 1;
			}
			if (op == VC4_PACKET_GL_INDEXED_PRIMITIVE && !addr_in_some_bo(rd32(p + off + 6), 1)) {
				FAIL("index buffer address %#x not in any BO", rd32(p + off + 6));
			}
			if (op == VC4_PACKET_GL_SHADER_STATE) {
				uint32_t a = rd32(p + off + 1);
				if (!addr_in_some_bo(a & ~0xfu, 36)) {
					FAIL("shader state address %#x not in any BO", a);
				}
			}
			if (op == VC4_PACKET_TILE_BINNING_MODE_CONFIG) {
				for (int i = 0; i < 3; i++) {
					if (!addr_in_some_bo(rd32(p + off + 1 + i * 4) , i == 1 ? 0 : 1) && i != 1) {
						FAIL("tile config address %d = %#x not in any BO", i, rd32(p + off + 1 + i * 4));
					}
				}
			}
			off += pkt_len[op];
		}
	}
	/* render CL, a BO of its own on the unref list */
	if (e->ct1ca != e->ct1ea) {
		bool found = false;
		for (int i = 0; i < g_all_count; i++) {
			struct drm_gem_cma_object *x = &g_all[i]->base;
			if (e->ct1ca >= x->paddr && e->ct1ea <= x->paddr + x->base.size && e->ct1ea >= e->ct1ca) {
				const uint8_t *p = (const uint8_t *)x->vaddr + (e->ct1ca - x->paddr);
				uint32_t n = e->ct1ea - e->ct1ca, off = 0;
				found = true;
				while (off < n) {
					uint8_t op = p[off];
					if (pkt_len[op] == 0 || off + pkt_len[op] > n) {
						FAIL("render CL bad packet %u at %u", op, off);
						return 1;
					}
					if (op == VC4_PACKET_BRANCH_TO_SUB_LIST && !addr_in_some_bo(rd32(p + off + 1), 32)) {
						FAIL("sub-list address %#x not in any BO", rd32(p + off + 1));
					}
					if ((op == VC4_PACKET_STORE_FULL_RES_TILE_BUFFER || op == VC4_PACKET_LOAD_FULL_RES_TILE_BUFFER) &&
					    !addr_in_some_bo(rd32(p + off + 1) & ~0xfu, VC4_TILE_BUFFER_SIZE)) {
						FAIL("full-res tile address %#x not in any BO", rd32(p + off + 1));
					}
					if ((op == VC4_PACKET_STORE_TILE_BUFFER_GENERAL || op == VC4_PACKET_LOAD_TILE_BUFFER_GENERAL)) {
						uint32_t a = rd32(p + off + 3) & ~0xfu;
						uint32_t bits = p[off + 1] & 7;
						if (bits != VC4_LOADSTORE_TILE_BUFFER_NONE && !addr_in_some_bo(a, 1)) {
							FAIL("general tile address %#x not in any BO", a);
						}
					}
					if (op == VC4_PACKET_TILE_RENDERING_MODE_CONFIG) {
						uint32_t a = rd32(p + off + 1);
						if (a != 0 && !addr_in_some_bo(a, 1)) {
							FAIL("render config address %#x not in any BO", a);
						}
					}
					off += pkt_len[op];
				}
				break;
			}
		}
		if (!found) {
			FAIL("render range %#x-%#x not inside any BO", e->ct1ca, e->ct1ea);
		}
	}
	/* shader records: every address the GPU will follow */
	if (e->exec_bo && e->shader_state_count > 0) {
		const uint8_t *rec = (const uint8_t *)e->exec_bo->vaddr +
		    ((e->args->bin_cl_size + 15u) & ~15u);
		for (uint32_t i = 0; i < e->shader_state_count; i++) {
			uint32_t addr_bits = e->shader_state[i].addr;
			uint32_t nattr = addr_bits & 7 ? addr_bits & 7 : 8;
			uint32_t size = (addr_bits & 8) ? 100 + nattr * 4 : 36 + nattr * 8;
			static const int code[3] = { 4, 16, 28 };
			for (int k = 0; k < 3; k++) {
				uint32_t a = rd32(rec + code[k]);
				bool is_shader = false;
				for (int b = 0; b < g_all_count; b++) {
					if (g_all[b]->validated_shader && g_all[b]->base.paddr == a) is_shader = true;
				}
				if (!is_shader) FAIL("record %u: code address %#x is not a validated shader", i, a);
				uint32_t u = rd32(rec + code[k] + 4);
				if (e->exec_bo && (u < e->exec_bo->paddr || u > e->exec_bo->paddr + e->exec_bo->base.size)) {
					FAIL("record %u: uniform address %#x outside exec BO", i, u);
				}
			}
			for (uint32_t k = 0; k < nattr; k++) {
				uint32_t a = rd32(rec + 36 + k * 8);
				if (!addr_in_some_bo(a, 1)) FAIL("record %u: attribute %u address %#x not in any BO", i, k, a);
			}
			rec += (size + 15u) & ~15u;
		}
	}
#undef FAIL
	return bad;
}

/* ---------------------------- tests ---------------------------- */

static int tests_run, tests_failed;
#define EXPECT(cond, ...) do { tests_run++; if (!(cond)) { tests_failed++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void
cleanup(struct vc4_exec_info *e)
{
	struct list_head *n = e->unref_list.next;
	while (n) {
		struct list_head *next = n->next;
		free((char *)n - offsetof(struct drm_vc4_bo, unref_head));
		n = next;
	}
	v3d_exec_release(e);
}

static void
test_accepts_basic(void)
{
	struct scenario s; struct vc4_exec_info e;
	scenario_basic(&s);
	int r = run(&s, &e);
	EXPECT(r == 0, "basic submission rejected: %d", r);
	if (r == 0) {
		EXPECT(e.ct0ca != e.ct0ea, "no bin list");
		EXPECT(e.ct1ca != e.ct1ea, "no render list");
		EXPECT(check_outputs(&e, "basic") == 0, "outputs outside BOs");
		/* the shader state packet must carry the relocated record address */
		const uint8_t *p = (const uint8_t *)e.exec_bo->vaddr;
		bool seen = false;
		for (uint32_t i = 0; i + 5 <= e.ct0ea - e.ct0ca; i++) {
			if (p[i] == VC4_PACKET_GL_SHADER_STATE && (rd32(p + i + 1) & ~0xfu) >= e.exec_bo->paddr) {
				seen = true;
			}
		}
		EXPECT(seen, "shader state not relocated into the exec BO");
	}
	cleanup(&e);
	world_reset();
}

typedef void (*mutate_fn)(struct scenario *);
static void m_no_flush(struct scenario *s) { s->bin.n -= 1; s->args.bin_cl_size = s->bin.n; }
static void m_unknown_op(struct scenario *s) { s->bin.d[0] = 200; }
static void m_prim_before_state(struct scenario *s)
{
	struct buf b; memset(&b, 0, sizeof(b));
	cl_tile_binning_config(&b, 1, 1, 0); cl_start_binning(&b); cl_array_prim(&b, 3, 0); cl_end(&b);
	s->bin = b; s->args.bin_cl_size = b.n;
}
static void m_start_before_config(struct scenario *s)
{
	struct buf b; memset(&b, 0, sizeof(b));
	cl_start_binning(&b); cl_tile_binning_config(&b, 1, 1, 0); cl_end(&b);
	s->bin = b; s->args.bin_cl_size = b.n;
}
static void m_zero_tiles(struct scenario *s) { s->bin.d[1 + 12] = 0; }
static void m_ms_flags(struct scenario *s) { s->bin.d[1 + 14] = VC4_BIN_CONFIG_TILE_BUFFER_64BIT; }
static void m_truncated_packet(struct scenario *s) { s->bin.n -= 1; s->bin.d[s->bin.n - 1] = VC4_PACKET_GL_ARRAY_PRIMITIVE; s->args.bin_cl_size = s->bin.n; }
static void m_vbo_too_small(struct scenario *s) { s->vbo->base.base.size = 8; }
static void m_stride_overrun(struct scenario *s)
{
	/* 3 vertices of stride 16 need 48+ bytes; ask for 1000 */
	s->bin.n = 0; memset(&s->bin, 0, sizeof(s->bin));
	cl_tile_binning_config(&s->bin, 1, 1, 0); cl_start_binning(&s->bin); cl_prim_list_format(&s->bin);
	cl_gem_handles(&s->bin, 2, 0); cl_shader_state(&s->bin, 1); cl_array_prim(&s->bin, 1000, 0); cl_end(&s->bin);
	s->args.bin_cl_size = s->bin.n;
}
static void m_ib_overflow(struct scenario *s)
{
	memset(&s->bin, 0, sizeof(s->bin));
	cl_tile_binning_config(&s->bin, 1, 1, 0); cl_start_binning(&s->bin); cl_prim_list_format(&s->bin);
	cl_gem_handles(&s->bin, 2, 0); cl_shader_state(&s->bin, 1); cl_indexed_prim(&s->bin, 1, 100000, 0, 2); cl_end(&s->bin);
	s->args.bin_cl_size = s->bin.n;
}
static void m_ib_offset_wrap(struct scenario *s)
{
	memset(&s->bin, 0, sizeof(s->bin));
	cl_tile_binning_config(&s->bin, 1, 1, 0); cl_start_binning(&s->bin); cl_prim_list_format(&s->bin);
	cl_gem_handles(&s->bin, 2, 0); cl_shader_state(&s->bin, 1); cl_indexed_prim(&s->bin, 1, 1, 0xfffffff0u, 0); cl_end(&s->bin);
	s->args.bin_cl_size = s->bin.n;
}
static void m_array_wrap(struct scenario *s)
{
	memset(&s->bin, 0, sizeof(s->bin));
	cl_tile_binning_config(&s->bin, 1, 1, 0); cl_start_binning(&s->bin); cl_prim_list_format(&s->bin);
	cl_gem_handles(&s->bin, 2, 0); cl_shader_state(&s->bin, 1); cl_array_prim(&s->bin, 0xffffff00u, 0x200); cl_end(&s->bin);
	s->args.bin_cl_size = s->bin.n;
}
static void m_shader_state_high_bits(struct scenario *s) { s->bin.d[1 + 12 + 3 + 1 + 1 + 1 + 1 + 8 + 1 + 1] = 0; /* placeholder, replaced below */ }
static void m_shader_handle_oob(struct scenario *s) { memcpy(&s->rec.d[0], &(uint32_t){ s->nbos }, 4); }   /* == bo_count */
static void m_shader_handle_not_shader(struct scenario *s) { memcpy(&s->rec.d[0], &(uint32_t){ 3 }, 4); }   /* the colour buffer */
static void m_shader_nonzero_offset(struct scenario *s) { uint32_t off = 8; memcpy(&s->rec.d[16 + 4], &off, 4); }
static void m_vbo_is_shader(struct scenario *s) { memcpy(&s->rec.d[12], &(uint32_t){ 0 }, 4); }             /* attribute BO = shader BO */
static void m_rec_too_short(struct scenario *s) { s->args.shader_rec_size = 20; }
static void m_rec_count_zero(struct scenario *s) { s->args.shader_rec_count = 0; }
static void m_color_too_small(struct scenario *s) { s->color->base.base.size = 100; }
static void m_color_bad_index(struct scenario *s) { s->args.color_write.hindex = 77; }
static void m_tile_oob(struct scenario *s) { s->args.max_x_tile = 1; }                                       /* bin config has 1 tile */
static void m_tiles_outside_target(struct scenario *s) { s->args.max_x_tile = 5; s->args.max_y_tile = 5; }
static void m_no_write_target(struct scenario *s) { s->args.color_write.hindex = 0xffffffffu; }
static void m_oversize_target(struct scenario *s) { s->args.width = 5000; }
static void m_bad_color_format(struct scenario *s) { s->args.color_write.bits = (3 << VC4_RENDER_CONFIG_FORMAT_SHIFT); }
static void m_shader_rec_overflow(struct scenario *s) { s->args.shader_rec_count = 70000; }
static void m_exec_overflow(struct scenario *s) { s->args.uniforms_size = 0xfffffff0u; }

static void
expect_reject(const char *name, mutate_fn fn)
{
	struct scenario s; struct vc4_exec_info e;
	scenario_basic(&s);
	fn(&s);
	int r = run(&s, &e);
	EXPECT(r != 0, "%s: accepted", name);
	cleanup(&e);
	world_reset();
}

/* --- shader validator tests --- */

static void
expect_shader(const char *name, const uint64_t *code, size_t n, bool ok)
{
	struct drm_vc4_bo *b = bo_new(n * 8);
	memcpy(b->base.vaddr, code, n * 8);
	struct vc4_validated_shader_info *i = vc4_validate_shader(&b->base);
	EXPECT((i != NULL) == ok, "shader %s: %s", name, i ? "accepted" : "rejected");
	v3d_validated_shader_free(i);
	world_reset();
}

static void
test_shaders(void)
{
	uint64_t c[16];
	simple_shader(c);
	expect_shader("minimal", c, 4, true);

	/* no PROG_END */
	for (int i = 0; i < 6; i++) c[i] = qpu_nop(QPU_SIG_NONE);
	expect_shader("no end", c, 6, false);

	/* PROG_END too close to the BO end (needs two delay slots) */
	c[0] = qpu_nop(QPU_SIG_NONE); c[1] = qpu_nop(QPU_SIG_PROG_END);
	expect_shader("truncated delay slots", c, 2, false);

	/* general VPM DMA write */
	simple_shader(c);
	c[0] = qpu_with_waddr(qpu_write(QPU_W_VPM_ADDR), QPU_W_VPM_ADDR);
	expect_shader("vpm_addr write", c, 4, false);

	/* host interrupt / mutex / noswap registers refused */
	simple_shader(c); c[0] = qpu_with_waddr(qpu_write(QPU_W_HOST_INT), QPU_W_HOST_INT);
	expect_shader("host_int", c, 4, false);
	simple_shader(c); c[0] = qpu_with_waddr(qpu_write(QPU_W_MUTEX_RELEASE), QPU_W_MUTEX_RELEASE);
	expect_shader("mutex_release", c, 4, false);

	/* a plain TMU address write with no uniform setup is a texture sample
	 * (allowed: recorded as one sample), a TMU write that is not "s" first
	 * and never submitted just sits in the setup state */
	simple_shader(c); c[0] = qpu_with_waddr(qpu_write(QPU_W_TMU0_S), QPU_W_TMU0_S);
	/* direct (non-uniform-relative) TMU load: rejected as not clamped */
	expect_shader("direct tmu without clamp", c, 4, false);

	/* unsupported signal: software breakpoint */
	simple_shader(c); c[0] = qpu_nop(QPU_SIG_SW_BREAKPOINT);
	expect_shader("breakpoint", c, 4, false);

	/* branch inside the delay slots of another branch */
	simple_shader(c);
	c[0] = qpu_nop(QPU_SIG_BRANCH) | QPU_BRANCH_REL | QPU_SET_FIELD(QPU_COND_BRANCH_ALWAYS, QPU_BRANCH_COND);
	c[1] = c[0];
	expect_shader("branch in delay slot", c, 4, false);

	/* branch to outside the shader */
	simple_shader(c);
	c[0] = QPU_SET_FIELD(QPU_SIG_BRANCH, QPU_SIG) | QPU_BRANCH_REL |
	    QPU_SET_FIELD(QPU_COND_BRANCH_ALWAYS, QPU_BRANCH_COND) |
	    QPU_SET_FIELD(QPU_W_NOP, QPU_WADDR_ADD) | QPU_SET_FIELD(QPU_W_NOP, QPU_WADDR_MUL) |
	    (uint64_t)(uint32_t)(1000 * 8);
	expect_shader("branch out of range", c, 4, false);

	/* absolute (non-relative) branch */
	simple_shader(c);
	c[0] = QPU_SET_FIELD(QPU_SIG_BRANCH, QPU_SIG) |
	    QPU_SET_FIELD(QPU_COND_BRANCH_ALWAYS, QPU_BRANCH_COND) |
	    QPU_SET_FIELD(QPU_W_NOP, QPU_WADDR_ADD) | QPU_SET_FIELD(QPU_W_NOP, QPU_WADDR_MUL);
	expect_shader("absolute branch", c, 4, false);
}

/* --- fuzzer --- */

static uint64_t g_rng = 88172645463325252ull;
static uint32_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return (uint32_t)(g_rng >> 11); }

static void
fuzz_submit(int iters)
{
	int accepted = 0;
	for (int it = 0; it < iters; it++) {
		struct scenario s; struct vc4_exec_info e;
		scenario_basic(&s);
		/* a few extra BOs of random size */
		for (int k = 0; k < (int)(rnd() % 4); k++) {
			s.bos[s.nbos++] = &bo_new(rnd() % 20000 + 1)->base;
		}
		s.args.bo_handle_count = s.nbos;
		/* mutate bytes */
		int nm = rnd() % 6;
		for (int k = 0; k < nm; k++) {
			switch (rnd() % 3) {
			case 0: if (s.bin.n) s.bin.d[rnd() % s.bin.n] = (uint8_t)rnd(); break;
			case 1: if (s.rec.n) s.rec.d[rnd() % s.rec.n] = (uint8_t)rnd(); break;
			case 2: if (s.bin.n) { uint32_t o = rnd() % s.bin.n; uint32_t v = rnd(); memcpy(&s.bin.d[o], &v, o + 4 <= s.bin.n ? 4 : 0); } break;
			}
		}
		if (rnd() % 4 == 0) s.args.max_x_tile = (uint8_t)rnd();
		if (rnd() % 4 == 0) s.args.max_y_tile = (uint8_t)rnd();
		if (rnd() % 4 == 0) s.args.width = (uint16_t)rnd();
		if (rnd() % 4 == 0) s.args.height = (uint16_t)rnd();
		if (rnd() % 5 == 0) s.args.color_write.bits = (uint16_t)rnd();
		if (rnd() % 6 == 0) s.args.shader_rec_count = rnd() % 4;
		if (rnd() % 6 == 0) s.args.bin_cl_size = rnd() % (s.bin.n + 8);
		if (rnd() % 6 == 0) s.args.shader_rec_size = rnd() % (s.rec.n + 8);
		if (rnd() % 8 == 0) s.args.flags = rnd();
		int r = run(&s, &e);
		if (r == 0) {
			accepted++;
			if (check_outputs(&e, "fuzz")) {
				tests_failed++;
			}
		}
		cleanup(&e);
		world_reset();
	}
	printf("fuzz_submit: %d iterations, %d accepted\n", iters, accepted);
}

static void
fuzz_shaders(int iters)
{
	int accepted = 0;
	for (int it = 0; it < iters; it++) {
		uint64_t code[24];
		size_t n = 4 + rnd() % 20;
		simple_shader(code);
		for (size_t i = 4; i < n; i++) code[i] = qpu_nop(QPU_SIG_NONE);
		code[n - 3] = qpu_nop(QPU_SIG_PROG_END);
		int nm = rnd() % 8;
		for (int k = 0; k < nm; k++) {
			uint64_t v = ((uint64_t)rnd() << 32) | rnd();
			size_t i = rnd() % n;
			switch (rnd() % 3) {
			case 0: code[i] = v; break;
			case 1: code[i] ^= (1ull << (rnd() % 64)); break;
			case 2: code[i] = (code[i] & ~(0x3full << 38)) | ((uint64_t)(rnd() % 64) << 38); break;
			}
		}
		struct drm_vc4_bo *b = bo_new(n * 8);
		memcpy(b->base.vaddr, code, n * 8);
		struct vc4_validated_shader_info *i = vc4_validate_shader(&b->base);
		if (i) {
			accepted++;
			/* sanity: recorded sizes are bounded by the program size */
			EXPECT(i->uniforms_size <= n * 8 / 8 * 4 * 2 + 8, "uniforms_size %u implausible", i->uniforms_size);
			EXPECT(i->uniforms_src_size >= i->uniforms_size, "src size < size");
		}
		v3d_validated_shader_free(i);
		world_reset();
	}
	printf("fuzz_shaders: %d iterations, %d accepted\n", iters, accepted);
}

/* The kernel-facing API (v3d_kglue.h): same submission through the glue. */
static void
test_glue(void)
{
	struct scenario s;
	scenario_basic(&s);
	struct v3d_kbo_desc d[MAX_BOS];
	for (uint32_t i = 0; i < s.nbos; i++) {
		struct drm_vc4_bo *b = (struct drm_vc4_bo *)s.bos[i];
		d[i].size = (uint32_t)b->base.base.size;
		d[i].gpu_addr = b->base.paddr;
		d[i].shader = b->validated_shader;
	}
	struct v3d_kresult r;
	int ret = v3d_kglue_validate(&s.args, d, s.nbos, s.bin.d, s.rec.d, s.uni.d, &g_ops, &r);
	EXPECT(ret == 0, "glue: basic submission rejected: %d", ret);
	EXPECT(r.ct0ca != r.ct0ea && r.ct1ca != r.ct1ea, "glue: empty lists");
	EXPECT(addr_in_some_bo(r.ct0ca, r.ct0ea - r.ct0ca), "glue: bin list outside created BOs");
	EXPECT(addr_in_some_bo(r.ct1ca, r.ct1ea - r.ct1ca), "glue: render list outside created BOs");

	/* a bad submission: error comes back as a positive errno */
	s.bin.d[0] = 200;
	ret = v3d_kglue_validate(&s.args, d, s.nbos, s.bin.d, s.rec.d, s.uni.d, &g_ops, &r);
	EXPECT(ret == EINVAL, "glue: bad opcode returned %d", ret);

	uint64_t code[4];
	simple_shader(code);
	struct vc4_validated_shader_info *si = v3d_kglue_validate_shader(code, sizeof(code));
	EXPECT(si != NULL, "glue: simple shader refused");
	v3d_kglue_free_shader(si);
	code[0] = qpu_with_waddr(qpu_write(QPU_W_VPM_ADDR), QPU_W_VPM_ADDR);
	EXPECT(v3d_kglue_validate_shader(code, sizeof(code)) == NULL, "glue: dangerous shader accepted");
	EXPECT(v3d_kglue_validate_shader(code, 12) == NULL, "glue: odd-size shader accepted");
	world_reset();
}

int
main(int argc, char **argv)
{
	g_dev.priv = &g_glue_ctx;
	int iters = argc > 1 ? atoi(argv[1]) : 20000;
	if (argc > 2) g_rng = strtoull(argv[2], NULL, 0) | 1;
	if (!getenv("V3D_TEST_VERBOSE")) { freopen("/dev/null", "w", stderr); }

	test_accepts_basic();
	expect_reject("no flush", m_no_flush);
	expect_reject("unknown opcode", m_unknown_op);
	expect_reject("primitive before shader state", m_prim_before_state);
	expect_reject("start binning before config", m_start_before_config);
	expect_reject("zero tiles", m_zero_tiles);
	expect_reject("unsupported bin flags", m_ms_flags);
	expect_reject("truncated packet", m_truncated_packet);
	expect_reject("vbo too small", m_vbo_too_small);
	expect_reject("stride overrun", m_stride_overrun);
	expect_reject("index buffer overflow", m_ib_overflow);
	expect_reject("index buffer offset wrap", m_ib_offset_wrap);
	expect_reject("array primitive wrap", m_array_wrap);
	expect_reject("shader handle == bo_count", m_shader_handle_oob);
	expect_reject("shader handle not a shader", m_shader_handle_not_shader);
	expect_reject("shader at nonzero offset", m_shader_nonzero_offset);
	expect_reject("attribute BO is a shader", m_vbo_is_shader);
	expect_reject("shader rec too short", m_rec_too_short);
	expect_reject("shader rec count zero", m_rec_count_zero);
	expect_reject("colour target too small", m_color_too_small);
	expect_reject("colour target bad index", m_color_bad_index);
	expect_reject("tile outside bin config", m_tile_oob);
	expect_reject("tiles outside target", m_tiles_outside_target);
	expect_reject("no write target", m_no_write_target);
	expect_reject("oversize target", m_oversize_target);
	expect_reject("bad colour format", m_bad_color_format);
	expect_reject("shader rec count huge", m_shader_rec_overflow);
	expect_reject("exec size overflow", m_exec_overflow);
	test_shaders();
	test_glue();
	(void)m_shader_state_high_bits;
	fuzz_submit(iters);
	fuzz_shaders(iters);

	printf("%d checks, %d failed\n", tests_run, tests_failed);
	return tests_failed ? 1 : 0;
}
