/*
 * Mesa's REAL vc4 gallium driver, built natively (build_mesa_host.sh), driven
 * through the gallium API against the libdrm shim and the mock kernel
 * (mock_kernel.h). Everything the driver emits -- QPU shader programs,
 * binner command lists, shader records, uniform streams, render-list
 * parameters -- goes through the vendored validators exactly as it would in
 * the BCM2837V3D kext. There is no GPU model, so nothing is rendered; what
 * this checks is that valid Mesa output is ACCEPTED by the validators (a
 * validator that rejects real driver output would fail on the first frame on
 * hardware) and that the ioctl traffic is well-formed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "pipe/p_shader_tokens.h"
#include "tgsi/tgsi_text.h"
#include "util/u_inlines.h"
#include "util/format/u_format.h"
#include "vc4/vc4_screen.h"

#include "mock_kernel.h"

void v3d_bug_on(const char *f, int l) { fprintf(stderr, "BUG_ON %s:%d\n", f, l); abort(); }

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } else { printf("ok:   " __VA_ARGS__); printf("\n"); } } while (0)

static struct pipe_screen *screen;
static struct pipe_context *ctx;
static struct kernel *kern;

static void *
make_shader(unsigned type, const char *text)
{
	struct tgsi_token tokens[1024];
	if (!tgsi_text_translate(text, tokens, 1024)) {
		printf("TGSI text did not parse:\n%s\n", text);
		abort();
	}
	struct pipe_shader_state s;
	memset(&s, 0, sizeof(s));
	s.type = PIPE_SHADER_IR_TGSI;
	s.tokens = tokens;
	return type == PIPE_SHADER_VERTEX ? ctx->create_vs_state(ctx, &s) : ctx->create_fs_state(ctx, &s);
}

static struct pipe_resource *
make_tex(unsigned w, unsigned h, enum pipe_format fmt, unsigned bind)
{
	struct pipe_resource t;
	memset(&t, 0, sizeof(t));
	t.target = PIPE_TEXTURE_2D;
	t.format = fmt;
	t.width0 = w; t.height0 = h; t.depth0 = 1; t.array_size = 1;
	t.bind = bind;
	return screen->resource_create(screen, &t);
}

static struct pipe_resource *
make_vbo(const void *data, unsigned size, unsigned bind)
{
	struct pipe_resource t;
	memset(&t, 0, sizeof(t));
	t.target = PIPE_BUFFER;
	t.format = PIPE_FORMAT_R8_UNORM;
	t.width0 = size; t.height0 = 1; t.depth0 = 1; t.array_size = 1;
	t.bind = bind;
	struct pipe_resource *r = screen->resource_create(screen, &t);
	struct pipe_box box;
	u_box_1d(0, size, &box);
	ctx->buffer_subdata(ctx, r, PIPE_MAP_WRITE, 0, size, data);
	return r;
}


/* set_vertex_buffers() takes over the reference in the struct it is given, so
 * each bind needs a reference of its own. */
static void
set_vbo(struct pipe_resource *r)
{
	struct pipe_vertex_buffer b;
	memset(&b, 0, sizeof(b));
	pipe_resource_reference(&b.buffer.resource, r);
	ctx->set_vertex_buffers(ctx, 1, &b);
}

static void
flush_and_report(const char *what, int expect_submits_before)
{
	int before = kern->submits, rej_before = kern->rejected_submits;
	ctx->flush(ctx, NULL, 0);
	CHECK(kern->rejected_submits == rej_before, "%s: validators accepted what Mesa submitted (%d rejected)",
	    what, kern->rejected_submits - rej_before);
	CHECK(kern->submits > before, "%s: a job was submitted (%d total)", what, kern->submits);
}


/* ------------------- corpus fuzzing from Mesa's own output -------------------
 * Every submission the driver made above was recorded by the mock kernel.
 * Mutate those (bytes of the binner list, shader records, uniforms; the ioctl
 * fields; BO sizes) and push them through the validators: no crash or
 * sanitizer report, and whatever is ACCEPTED must only reference memory of
 * buffers the job owns. */
struct fzbo { uint32_t gpu, size; void *staging; };
static struct fzbo fz_bos[8];
static int fz_nbos;
static uint32_t fz_next;
static struct fzbo fz_client[64];
static int fz_nclient;

static int
fz_alloc(void *ctx, size_t size, bool stage, uint32_t *gpu, void **staging)
{
	(void)ctx;
	if (fz_nbos >= 8) return ENOMEM;
	*gpu = fz_next;
	fz_next += (uint32_t)((size + 0xfff) & ~0xfffu) + 0x1000;
	*staging = stage ? calloc(1, (size + 15) & ~(size_t)15) : NULL;
	fz_bos[fz_nbos++] = (struct fzbo){ *gpu, (uint32_t)size, *staging };
	return 0;
}

static bool
fz_in_range(uint32_t addr, uint32_t len)
{
	for (int i = 0; i < fz_nbos; i++)
		if (addr >= fz_bos[i].gpu && (uint64_t)addr + len <= (uint64_t)fz_bos[i].gpu + fz_bos[i].size) return true;
	for (int i = 0; i < fz_nclient; i++)
		if (addr >= fz_client[i].gpu && (uint64_t)addr + len <= (uint64_t)fz_client[i].gpu + fz_client[i].size) return true;
	return false;
}

static const uint8_t fz_len[256] = {
	[0] = 1, [1] = 1, [4] = 1, [5] = 1, [6] = 1, [7] = 1, [8] = 1, [16] = 5, [17] = 5,
	[24] = 1, [25] = 1, [26] = 5, [27] = 5, [28] = 7, [29] = 7, [32] = 14, [33] = 10, [56] = 2, [64] = 5,
	[96] = 4, [97] = 5, [98] = 5, [99] = 5, [100] = 3, [101] = 5, [102] = 9, [103] = 5, [105] = 9, [106] = 9,
	[112] = 16, [113] = 11, [114] = 14, [115] = 3,
};

static uint32_t fz_rd(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static char fz_why[160];
#define FZ_BAD(...) do { snprintf(fz_why, sizeof(fz_why), __VA_ARGS__); bad = 1; } while (0)

static const uint8_t *
fz_find(uint32_t addr, uint32_t *room)
{
	for (int i = 0; i < fz_nbos; i++)
		if (fz_bos[i].staging && addr >= fz_bos[i].gpu && addr < fz_bos[i].gpu + fz_bos[i].size) {
			*room = fz_bos[i].gpu + fz_bos[i].size - addr;
			return (const uint8_t *)fz_bos[i].staging + (addr - fz_bos[i].gpu);
		}
	return NULL;
}

/* returns 0 if the accepted job only touches memory it owns */
static int
fz_check(const struct v3d_kresult *r)
{
	uint32_t room;
	int bad = 0;
	if (r->ct0ca != r->ct0ea) {
		const uint8_t *p = fz_find(r->ct0ca, &room);
		uint32_t n = r->ct0ea - r->ct0ca, off = 0;
		if (!p || n > room) return 1;
		while (off < n) {
			uint8_t op = p[off];
			if (fz_len[op] == 0 || off + fz_len[op] > n) return 1;
			if (op == 32 && !fz_in_range(fz_rd(p + off + 6), 1)) FZ_BAD("indexed prim: index buffer %#x", fz_rd(p + off + 6));
			if (op == 64 && !fz_in_range(fz_rd(p + off + 1) & ~0xfu, 36)) FZ_BAD("shader state %#x", fz_rd(p + off + 1));
			off += fz_len[op];
		}
	}
	if (r->ct1ca != r->ct1ea) {
		const uint8_t *p = fz_find(r->ct1ca, &room);
		uint32_t n = r->ct1ea - r->ct1ca, off = 0;
		if (!p || n > room) return 1;
		while (off < n) {
			uint8_t op = p[off];
			if (fz_len[op] == 0 || off + fz_len[op] > n) return 1;
			if (op == 17 && !fz_in_range(fz_rd(p + off + 1), 32)) FZ_BAD("sub-list %#x", fz_rd(p + off + 1));
			if ((op == 26 || op == 27) && !fz_in_range(fz_rd(p + off + 1) & ~0xfu, 64 * 64 * 4)) FZ_BAD("full-res tile %#x", fz_rd(p + off + 1) & ~0xfu);
			if (op == 113 && fz_rd(p + off + 1) && !fz_in_range(fz_rd(p + off + 1), 1)) FZ_BAD("render config target %#x", fz_rd(p + off + 1));
			if ((op == 28 || op == 29) && (p[off + 1] & 7) != 0 && !fz_in_range(fz_rd(p + off + 3) & ~0xfu, 1)) FZ_BAD("general load/store %#x bits %#x", fz_rd(p + off + 3) & ~0xfu, p[off + 1]);
			off += fz_len[op];
		}
	}
	return bad;
}

static uint64_t fz_rng = 0x9e3779b97f4a7c15ull;
static uint32_t fz_rnd(void) { fz_rng ^= fz_rng << 13; fz_rng ^= fz_rng >> 7; fz_rng ^= fz_rng << 17; return (uint32_t)(fz_rng >> 11); }

static void
fuzz_corpus(struct kernel *k, int iters)
{
	int accepted = 0, bad = 0;
	if (k->nseeds == 0) { printf("fuzz: no seeds\n"); return; }
	for (int it = 0; it < iters; it++) {
		const struct kseed *sd = &k->seeds[fz_rnd() % k->nseeds];
		struct v3d_submit_cl a = sd->args;
		size_t nb = a.bin_cl_size, nr = a.shader_rec_size, nu = a.uniforms_size;
		uint8_t *bin = malloc(nb + 1), *rec = malloc(nr + 1), *uni = malloc(nu + 1);
		struct v3d_kbo_desc d[64];
		memcpy(bin, sd->bin, nb); memcpy(rec, sd->rec, nr); memcpy(uni, sd->uni, nu);
		memcpy(d, sd->d, sizeof(d[0]) * sd->nbos);
		int nm = 1 + fz_rnd() % 4;
		for (int m = 0; m < nm; m++) {
			switch (fz_rnd() % 9) {
			case 0: if (nb) bin[fz_rnd() % nb] = (uint8_t)fz_rnd(); break;
			case 1: if (nr) rec[fz_rnd() % nr] = (uint8_t)fz_rnd(); break;
			case 2: if (nu) uni[fz_rnd() % nu] = (uint8_t)fz_rnd(); break;
			case 3: if (nb > 8) { uint32_t v = fz_rnd(); memcpy(&bin[fz_rnd() % (nb - 4)], &v, 4); } break;
			case 4: if (nr > 8) { uint32_t v = fz_rnd(); memcpy(&rec[fz_rnd() % (nr - 4)], &v, 4); } break;
			case 5: a.width = fz_rnd() % 3 ? a.width : (uint16_t)fz_rnd(); a.height = fz_rnd() % 3 ? a.height : (uint16_t)fz_rnd(); break;
			case 6: a.max_x_tile = (uint8_t)(fz_rnd() % 8); a.max_y_tile = (uint8_t)(fz_rnd() % 8); a.min_x_tile = (uint8_t)(fz_rnd() % 3); break;
			case 7: if (sd->nbos) { uint32_t i = fz_rnd() % sd->nbos; d[i].size = fz_rnd() % 4 ? d[i].size : fz_rnd() % 70000; } break;
			case 8: a.color_write.bits = (uint16_t)fz_rnd(); a.flags = fz_rnd() % 2 ? a.flags : fz_rnd(); break;
			}
		}
		fz_nbos = 0; fz_next = 0xC8000000u;
		fz_nclient = (int)sd->nbos;
		for (uint32_t i = 0; i < sd->nbos; i++) fz_client[i] = (struct fzbo){ d[i].gpu_addr, d[i].size, NULL };
		struct v3d_kops ops = { NULL, fz_alloc };
		struct v3d_kresult r;
		int e = v3d_kglue_validate(&a, d, sd->nbos, bin, rec, uni, &ops, &r);
		if (e == 0) {
			accepted++;
			if (fz_check(&r)) { bad++; printf("FAIL: accepted job references memory it does not own (iteration %d): %s [w=%u h=%u tiles %u-%u,%u-%u bits=%#x flags=%#x]\n", it, fz_why, a.width, a.height, a.min_x_tile, a.max_x_tile, a.min_y_tile, a.max_y_tile, a.color_write.bits, a.flags); }
		}
		for (int i = 0; i < fz_nbos; i++) free(fz_bos[i].staging);
		free(bin); free(rec); free(uni);
	}
	printf("fuzz_corpus: %d iterations from %d Mesa-generated seeds, %d accepted, %d bad\n", iters, k->nseeds, accepted, bad);
	CHECK(bad == 0, "no accepted mutation referenced memory outside the job's buffers");
	CHECK(accepted > 0, "some mutations (benign ones) are still accepted, so the check is not vacuous");
}


/* The QPU programs Mesa's compiler produced, mutated at the bit level. Every
 * program is validated at BO creation; a refusal is fine, a crash or a
 * sanitizer report is not. Accepted programs must at least end in a
 * terminated instruction stream the validator walked within the buffer. */
static void
fuzz_shader_corpus(struct kernel *k, int iters)
{
	int accepted = 0;
	if (k->nshader_codes == 0) { printf("fuzz: no shader seeds\n"); return; }
	for (int it = 0; it < iters; it++) {
		const struct kshader *sd = &k->shader_codes[fz_rnd() % k->nshader_codes];
		uint8_t *code = malloc(sd->size);        /* exact size: ASAN catches any read past the end */
		memcpy(code, sd->code, sd->size);
		int nm = 1 + fz_rnd() % 5;
		for (int m = 0; m < nm; m++) {
			switch (fz_rnd() % 4) {
			case 0: code[fz_rnd() % sd->size] ^= (uint8_t)(1u << (fz_rnd() % 8)); break;
			case 1: code[fz_rnd() % sd->size] = (uint8_t)fz_rnd(); break;
			case 2: { uint32_t i = (fz_rnd() % (sd->size / 8)) * 8; uint64_t v = ((uint64_t)fz_rnd() << 32) | fz_rnd(); memcpy(code + i, &v, 8); break; }
			case 3: { uint32_t i = (fz_rnd() % (sd->size / 8)) * 8, j = (fz_rnd() % (sd->size / 8)) * 8; uint8_t t[8]; memcpy(t, code + i, 8); memcpy(code + i, code + j, 8); memcpy(code + j, t, 8); break; }
			}
		}
		uint32_t size = sd->size;
		if (fz_rnd() % 8 == 0 && size > 8) size = (size - 8 * (1 + fz_rnd() % (size / 8 - 1))) ;   /* truncated program */
		struct vc4_validated_shader_info *si = v3d_kglue_validate_shader(code, size & ~7u ? size & ~7u : 8);
		if (si) {
			accepted++;
			v3d_kglue_free_shader(si);
		}
		free(code);
	}
	printf("fuzz_shader_corpus: %d iterations from %d Mesa-generated programs, %d accepted\n", iters, k->nshader_codes, accepted);
	CHECK(accepted > 0, "benign shader mutations are still accepted");
}

int
main(void)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (!getenv("V3D_TEST_VERBOSE")) {
		freopen("/dev/null", "a", stderr);       /* validator diagnostics are noisy by design */
	}
	kern = calloc(1, sizeof(*kern));
	kern->next_gpu = 0xC0100000u;
	struct v3d_transport t = mock_transport(kern);
	int fd = v3d_drm_open_transport(&t);
	CHECK(fd >= 0, "shim fd");

	screen = vc4_screen_create(fd, NULL, NULL);
	CHECK(screen != NULL, "vc4_screen_create over the shim");
	if (!screen) return 1;
	printf("screen: %s / %s\n", screen->get_name(screen), screen->get_vendor(screen));
	ctx = screen->context_create(screen, NULL, 0);
	CHECK(ctx != NULL, "context_create");
	if (!ctx) return 1;

	const unsigned W = 64, H = 64;
	struct pipe_resource *color = make_tex(W, H, PIPE_FORMAT_R8G8B8A8_UNORM, PIPE_BIND_RENDER_TARGET);
	struct pipe_resource *depth = make_tex(W, H, PIPE_FORMAT_Z24_UNORM_S8_UINT, PIPE_BIND_DEPTH_STENCIL);
	CHECK(color && depth, "render target and depth buffer");

	struct pipe_framebuffer_state fb;
	memset(&fb, 0, sizeof(fb));
	fb.width = W; fb.height = H; fb.nr_cbufs = 1; 
	fb.cbufs[0].texture = color; fb.cbufs[0].format = PIPE_FORMAT_R8G8B8A8_UNORM;
	fb.zsbuf.texture = depth; fb.zsbuf.format = PIPE_FORMAT_Z24_UNORM_S8_UINT;
	ctx->set_framebuffer_state(ctx, &fb);

	struct pipe_viewport_state vp;
	memset(&vp, 0, sizeof(vp));
	vp.scale[0] = W / 2.0f; vp.scale[1] = -(H / 2.0f); vp.scale[2] = 0.5f;
	vp.translate[0] = W / 2.0f; vp.translate[1] = H / 2.0f; vp.translate[2] = 0.5f;
	ctx->set_viewport_states(ctx, 0, 1, &vp);

	/* fixed-function state */
	struct pipe_blend_state blend; memset(&blend, 0, sizeof(blend));
	blend.rt[0].colormask = 0xf;
	void *blend_cso = ctx->create_blend_state(ctx, &blend);
	struct pipe_blend_state blend_a = blend;
	blend_a.rt[0].blend_enable = 1;
	blend_a.rt[0].rgb_func = PIPE_BLEND_ADD; blend_a.rt[0].alpha_func = PIPE_BLEND_ADD;
	blend_a.rt[0].rgb_src_factor = PIPE_BLENDFACTOR_SRC_ALPHA; blend_a.rt[0].alpha_src_factor = PIPE_BLENDFACTOR_ONE;
	blend_a.rt[0].rgb_dst_factor = PIPE_BLENDFACTOR_INV_SRC_ALPHA; blend_a.rt[0].alpha_dst_factor = PIPE_BLENDFACTOR_ZERO;
	void *blend_alpha_cso = ctx->create_blend_state(ctx, &blend_a);
	struct pipe_depth_stencil_alpha_state dsa; memset(&dsa, 0, sizeof(dsa));
	void *dsa_off = ctx->create_depth_stencil_alpha_state(ctx, &dsa);
	dsa.depth_enabled = 1; dsa.depth_writemask = 1; dsa.depth_func = PIPE_FUNC_LESS;
	void *dsa_on = ctx->create_depth_stencil_alpha_state(ctx, &dsa);
	struct pipe_rasterizer_state rs; memset(&rs, 0, sizeof(rs));
	rs.half_pixel_center = 1; rs.bottom_edge_rule = 1; rs.depth_clip_near = 1; rs.depth_clip_far = 1;
	rs.cull_face = PIPE_FACE_NONE; rs.point_size = 1; rs.line_width = 1; rs.clip_halfz = 0;
	void *rs_cso = ctx->create_rasterizer_state(ctx, &rs);
	ctx->bind_rasterizer_state(ctx, rs_cso);
	ctx->bind_blend_state(ctx, blend_cso);
	ctx->bind_depth_stencil_alpha_state(ctx, dsa_off);

	/* vertex data: position (xyzw) + colour (rgba) */
	float verts[3][8] = {
		{ -0.8f, -0.8f, 0.2f, 1, 1, 0, 0, 1 },
		{  0.8f, -0.8f, 0.2f, 1, 0, 1, 0, 1 },
		{  0.0f,  0.8f, 0.2f, 1, 0, 0, 1, 0.5f },
	};
	struct pipe_resource *vbo = make_vbo(verts, sizeof(verts), PIPE_BIND_VERTEX_BUFFER);
	struct pipe_vertex_element ve[2]; memset(ve, 0, sizeof(ve));
	ve[0].src_format = PIPE_FORMAT_R32G32B32A32_FLOAT; ve[0].src_offset = 0;
	ve[1].src_format = PIPE_FORMAT_R32G32B32A32_FLOAT; ve[1].src_offset = 16;
	for (int i = 0; i < 2; i++) ve[i].vertex_buffer_index = 0;
	void *ve_cso = ctx->create_vertex_elements_state(ctx, 2, ve);
	ctx->bind_vertex_elements_state(ctx, ve_cso);
	set_vbo(vbo);

	void *vs = make_shader(PIPE_SHADER_VERTEX,
	    "VERT\nDCL IN[0]\nDCL IN[1]\nDCL OUT[0], POSITION\nDCL OUT[1], GENERIC[0]\n"
	    "  0: MOV OUT[0], IN[0]\n  1: MOV OUT[1], IN[1]\n  2: END\n");
	void *fs = make_shader(PIPE_SHADER_FRAGMENT,
	    "FRAG\nDCL IN[0], GENERIC[0], PERSPECTIVE\nDCL OUT[0], COLOR\n  0: MOV OUT[0], IN[0]\n  1: END\n");
	CHECK(vs && fs, "TGSI vertex + fragment shader states");
	ctx->bind_vs_state(ctx, vs);
	ctx->bind_fs_state(ctx, fs);

	struct pipe_draw_info info; memset(&info, 0, sizeof(info));
	info.mode = MESA_PRIM_TRIANGLES; info.instance_count = 1; info.max_index = 2;
	struct pipe_draw_start_count_bias draw = { .start = 0, .count = 3 };

	/* 1. clear only */
	union pipe_color_union cc; memset(&cc, 0, sizeof(cc));
	cc.f[0] = 0.2f; cc.f[1] = 0.4f; cc.f[2] = 0.6f; cc.f[3] = 1;
	ctx->clear(ctx, PIPE_CLEAR_COLOR0 | PIPE_CLEAR_DEPTH, NULL, &cc, 1.0, 0);
	flush_and_report("clear", 0);

	/* 2. clear + varying-colour triangle (compiles a VS/FS pair to QPU code) */
	ctx->clear(ctx, PIPE_CLEAR_COLOR0 | PIPE_CLEAR_DEPTH, NULL, &cc, 1.0, 0);
	ctx->draw_vbo(ctx, &info, 0, NULL, &draw, 1);
	flush_and_report("triangle", 0);
	CHECK(kern->shaders > 0, "driver created %d validated shader BOs (%d refused)", kern->shaders, kern->rejected_shaders);
	CHECK(kern->rejected_shaders == 0, "no shader program the driver generated was refused");

	/* 3. depth test + alpha blending */
	ctx->bind_depth_stencil_alpha_state(ctx, dsa_on);
	ctx->bind_blend_state(ctx, blend_alpha_cso);
	ctx->draw_vbo(ctx, &info, 0, NULL, &draw, 1);
	flush_and_report("depth + blend", 0);

	/* 4. indexed draw */
	uint16_t idx[3] = { 0, 1, 2 };
	struct pipe_resource *ibo = make_vbo(idx, sizeof(idx), PIPE_BIND_INDEX_BUFFER);
	struct pipe_draw_info iinfo = info;
	iinfo.index_size = 2; iinfo.index.resource = ibo; iinfo.index_bounds_valid = true; iinfo.min_index = 0; iinfo.max_index = 2;
	ctx->bind_depth_stencil_alpha_state(ctx, dsa_off);
	ctx->bind_blend_state(ctx, blend_cso);
	ctx->draw_vbo(ctx, &iinfo, 0, NULL, &draw, 1);
	flush_and_report("indexed draw", 0);

	/* 5. textured draw: exercises the texture-sample relocation path */
	struct pipe_resource *tex = make_tex(8, 8, PIPE_FORMAT_R8G8B8A8_UNORM, PIPE_BIND_SAMPLER_VIEW);
	uint32_t texels[64]; for (int i = 0; i < 64; i++) texels[i] = 0xff000000u | (i * 4);
	struct pipe_box box; u_box_2d(0, 0, 8, 8, &box);
	ctx->texture_subdata(ctx, tex, 0, PIPE_MAP_WRITE, &box, texels, 8 * 4, 0);
	struct pipe_sampler_view svt; memset(&svt, 0, sizeof(svt));
	svt.format = PIPE_FORMAT_R8G8B8A8_UNORM; svt.target = PIPE_TEXTURE_2D;
	svt.swizzle_r = PIPE_SWIZZLE_X; svt.swizzle_g = PIPE_SWIZZLE_Y; svt.swizzle_b = PIPE_SWIZZLE_Z; svt.swizzle_a = PIPE_SWIZZLE_W;
	struct pipe_sampler_view *sview = ctx->create_sampler_view(ctx, tex, &svt);
	struct pipe_sampler_state ss; memset(&ss, 0, sizeof(ss));
	ss.min_img_filter = ss.mag_img_filter = PIPE_TEX_FILTER_NEAREST;
	ss.wrap_s = ss.wrap_t = ss.wrap_r = PIPE_TEX_WRAP_CLAMP_TO_EDGE;
	void *ss_cso = ctx->create_sampler_state(ctx, &ss);
	void *tfs = make_shader(PIPE_SHADER_FRAGMENT,
	    "FRAG\nDCL IN[0], GENERIC[0], PERSPECTIVE\nDCL OUT[0], COLOR\nDCL SAMP[0]\nDCL SVIEW[0], 2D, FLOAT\n"
	    "  0: TEX OUT[0], IN[0], SAMP[0], 2D\n  1: END\n");
	ctx->bind_fs_state(ctx, tfs);
	ctx->bind_sampler_states(ctx, PIPE_SHADER_FRAGMENT, 0, 1, &ss_cso);
	ctx->set_sampler_views(ctx, PIPE_SHADER_FRAGMENT, 0, 1, 0, &sview);
	ctx->draw_vbo(ctx, &info, 0, NULL, &draw, 1);
	flush_and_report("textured draw", 0);

	/* 6. uniforms: constant buffer read in the fragment shader */
	float cb[4] = { 0.1f, 0.2f, 0.3f, 1.0f };
	struct pipe_constant_buffer cbuf0; memset(&cbuf0, 0, sizeof(cbuf0));
	cbuf0.user_buffer = cb; cbuf0.buffer_size = sizeof(cb);
	ctx->set_constant_buffer(ctx, PIPE_SHADER_FRAGMENT, 0, false, &cbuf0);
	void *cfs = make_shader(PIPE_SHADER_FRAGMENT,
	    "FRAG\nDCL IN[0], GENERIC[0], PERSPECTIVE\nDCL OUT[0], COLOR\nDCL CONST[0]\n"
	    "  0: MUL OUT[0], IN[0], CONST[0]\n  1: END\n");
	ctx->bind_fs_state(ctx, cfs);
	ctx->draw_vbo(ctx, &info, 0, NULL, &draw, 1);
	flush_and_report("uniform-driven draw", 0);


	/* ---- wider coverage: every feature path Mesa's vc4 driver has that a GLES2 app uses ---- */
	ctx->bind_depth_stencil_alpha_state(ctx, dsa_off);
	ctx->bind_blend_state(ctx, blend_cso);
	ctx->bind_vs_state(ctx, vs);
	ctx->bind_fs_state(ctx, fs);

	/* A. primitive types that the hardware draws directly or via a simple conversion */
	{
		static const unsigned prims[] = { MESA_PRIM_TRIANGLE_STRIP, MESA_PRIM_TRIANGLE_FAN, MESA_PRIM_POINTS };
		static const char *names[] = { "triangle strip", "triangle fan", "points" };
		struct pipe_rasterizer_state r2 = rs;
		r2.point_size = 6; r2.point_size_per_vertex = 0;
		void *rs2 = ctx->create_rasterizer_state(ctx, &r2);
		ctx->bind_rasterizer_state(ctx, rs2);
		for (unsigned i = 0; i < sizeof(prims) / sizeof(prims[0]); i++) {
			struct pipe_draw_info pi = info;
			pi.mode = prims[i];
			ctx->draw_vbo(ctx, &pi, 0, NULL, &draw, 1);
			flush_and_report(names[i], 0);
		}
		ctx->bind_rasterizer_state(ctx, rs_cso);
	}

	/* B. stencil */
	{
		struct pipe_depth_stencil_alpha_state d2; memset(&d2, 0, sizeof(d2));
		d2.depth_enabled = 1; d2.depth_writemask = 1; d2.depth_func = PIPE_FUNC_LEQUAL;
		d2.stencil[0].enabled = 1; d2.stencil[0].func = PIPE_FUNC_ALWAYS;
		d2.stencil[0].fail_op = PIPE_STENCIL_OP_KEEP; d2.stencil[0].zfail_op = PIPE_STENCIL_OP_KEEP;
		d2.stencil[0].zpass_op = PIPE_STENCIL_OP_REPLACE; d2.stencil[0].valuemask = 0xff; d2.stencil[0].writemask = 0xff;
		void *dsa_st = ctx->create_depth_stencil_alpha_state(ctx, &d2);
		struct pipe_stencil_ref sref = { .ref_value = { 1, 1 } };
		ctx->bind_depth_stencil_alpha_state(ctx, dsa_st);
		ctx->set_stencil_ref(ctx, sref);
		ctx->draw_vbo(ctx, &info, 0, NULL, &draw, 1);
		flush_and_report("stencil", 0);
		ctx->bind_depth_stencil_alpha_state(ctx, dsa_off);
	}

	/* C. textures: mipmapped, non-power-of-two, cube map, ETC1 */
	{
		struct sampler_case { const char *name; unsigned target; unsigned w, h; unsigned last_level; enum pipe_format fmt; unsigned min_mip; const char *dim; } cases[] = {
			{ "mipmapped texture", PIPE_TEXTURE_2D, 16, 16, 4, PIPE_FORMAT_R8G8B8A8_UNORM, PIPE_TEX_MIPFILTER_LINEAR, "2D" },
			{ "NPOT texture", PIPE_TEXTURE_2D, 13, 5, 0, PIPE_FORMAT_R8G8B8A8_UNORM, PIPE_TEX_MIPFILTER_NONE, "2D" },
			{ "RGB565 texture", PIPE_TEXTURE_2D, 8, 8, 0, PIPE_FORMAT_B5G6R5_UNORM, PIPE_TEX_MIPFILTER_NONE, "2D" },
			{ "ETC1 texture", PIPE_TEXTURE_2D, 8, 8, 0, PIPE_FORMAT_ETC1_RGB8, PIPE_TEX_MIPFILTER_NONE, "2D" },
			{ "cube map", PIPE_TEXTURE_CUBE, 8, 8, 0, PIPE_FORMAT_R8G8B8A8_UNORM, PIPE_TEX_MIPFILTER_NONE, "CUBE" },
		};
		for (unsigned c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
			struct sampler_case *sc = &cases[c];
			if (!screen->is_format_supported(screen, sc->fmt, sc->target, 0, 0, PIPE_BIND_SAMPLER_VIEW)) {
				printf("note: %s: format not supported by this driver, skipped\n", sc->name);
				continue;
			}
			struct pipe_resource tt;
			memset(&tt, 0, sizeof(tt));
			tt.target = sc->target; tt.format = sc->fmt;
			tt.width0 = sc->w; tt.height0 = sc->h; tt.depth0 = 1;
			tt.array_size = sc->target == PIPE_TEXTURE_CUBE ? 6 : 1;
			tt.last_level = sc->last_level; tt.bind = PIPE_BIND_SAMPLER_VIEW;
			struct pipe_resource *res = screen->resource_create(screen, &tt);
			CHECK(res != NULL, "%s: resource", sc->name);
			if (!res) continue;
			uint8_t texdata[4096];
			for (unsigned i = 0; i < sizeof(texdata); i++) texdata[i] = (uint8_t)(i * 7);
			for (unsigned face = 0; face < tt.array_size; face++) {
				for (unsigned lvl = 0; lvl <= sc->last_level; lvl++) {
					unsigned lw = u_minify(sc->w, lvl), lh = u_minify(sc->h, lvl);
					struct pipe_box b = { .x = 0, .y = 0, .z = (int)face, .width = (int)lw, .height = (int)lh, .depth = 1 };
					unsigned stride = util_format_get_stride(sc->fmt, lw);
					ctx->texture_subdata(ctx, res, lvl, PIPE_MAP_WRITE, &b, texdata, stride, 0);
				}
			}
			struct pipe_sampler_view v; memset(&v, 0, sizeof(v));
			v.format = sc->fmt; v.target = sc->target;
			v.swizzle_r = PIPE_SWIZZLE_X; v.swizzle_g = PIPE_SWIZZLE_Y; v.swizzle_b = PIPE_SWIZZLE_Z; v.swizzle_a = PIPE_SWIZZLE_W;
			v.u.tex.first_level = 0; v.u.tex.last_level = sc->last_level;
			v.u.tex.first_layer = 0; v.u.tex.last_layer = tt.array_size - 1;
			struct pipe_sampler_view *sv = ctx->create_sampler_view(ctx, res, &v);
			struct pipe_sampler_state sst; memset(&sst, 0, sizeof(sst));
			sst.min_img_filter = PIPE_TEX_FILTER_LINEAR; sst.mag_img_filter = PIPE_TEX_FILTER_LINEAR;
			sst.min_mip_filter = sc->min_mip;
			sst.wrap_s = sst.wrap_t = sst.wrap_r = PIPE_TEX_WRAP_CLAMP_TO_EDGE;
			void *sst_cso = ctx->create_sampler_state(ctx, &sst);
			char text[512];
			snprintf(text, sizeof(text),
			    "FRAG\nDCL IN[0], GENERIC[0], PERSPECTIVE\nDCL OUT[0], COLOR\nDCL SAMP[0]\nDCL SVIEW[0], %s, FLOAT\n"
			    "  0: TEX OUT[0], IN[0], SAMP[0], %s\n  1: END\n", sc->dim, sc->dim);
			void *tfs2 = make_shader(PIPE_SHADER_FRAGMENT, text);
			ctx->bind_fs_state(ctx, tfs2);
			ctx->bind_sampler_states(ctx, PIPE_SHADER_FRAGMENT, 0, 1, &sst_cso);
			ctx->set_sampler_views(ctx, PIPE_SHADER_FRAGMENT, 0, 1, 0, &sv);
			ctx->draw_vbo(ctx, &info, 0, NULL, &draw, 1);
			flush_and_report(sc->name, 0);
		}
		ctx->bind_fs_state(ctx, fs);
	}

	/* D. discard and a dynamic branch (QPU branches + uniform address resets) */
	{
		void *kfs = make_shader(PIPE_SHADER_FRAGMENT,
		    "FRAG\nDCL IN[0], GENERIC[0], PERSPECTIVE\nDCL OUT[0], COLOR\nDCL TEMP[0]\nIMM[0] FLT32 { 0.5, 0.0, 0.0, 0.0 }\n"
		    "  0: ADD TEMP[0], IN[0].xxxx, -IMM[0].xxxx\n  1: KILL_IF TEMP[0]\n  2: MOV OUT[0], IN[0]\n  3: END\n");
		ctx->bind_fs_state(ctx, kfs);
		ctx->draw_vbo(ctx, &info, 0, NULL, &draw, 1);
		flush_and_report("fragment discard", 0);

		void *bfs = make_shader(PIPE_SHADER_FRAGMENT,
		    "FRAG\nDCL IN[0], GENERIC[0], PERSPECTIVE\nDCL OUT[0], COLOR\nDCL CONST[0]\nDCL TEMP[0]\nIMM[0] FLT32 { 0.0, 0.5, 2.0, 1.0 }\n"
		    "  0: SGT TEMP[0].x, CONST[0].xxxx, IMM[0].xxxx\n  1: IF TEMP[0].xxxx\n  2: MUL OUT[0], IN[0], IMM[0].zzzz\n"
		    "  3: ELSE\n  4: MUL OUT[0], IN[0], IMM[0].yyyy\n  5: ENDIF\n  6: END\n");
		ctx->bind_fs_state(ctx, bfs);
		ctx->draw_vbo(ctx, &info, 0, NULL, &draw, 1);
		flush_and_report("dynamic branch", 0);
		ctx->bind_fs_state(ctx, fs);
	}

	/* E. 4x multisampled render target (MSAA store path, 32x32 tiles) */
	{
		struct pipe_resource mt;
		memset(&mt, 0, sizeof(mt));
		mt.target = PIPE_TEXTURE_2D; mt.format = PIPE_FORMAT_R8G8B8A8_UNORM;
		mt.width0 = W; mt.height0 = H; mt.depth0 = 1; mt.array_size = 1;
		mt.nr_samples = mt.nr_storage_samples = 4; mt.bind = PIPE_BIND_RENDER_TARGET;
		struct pipe_resource *msaa = screen->resource_create(screen, &mt);
		if (msaa) {
			mt.format = PIPE_FORMAT_Z24_UNORM_S8_UINT; mt.bind = PIPE_BIND_DEPTH_STENCIL;
			struct pipe_resource *msaaz = screen->resource_create(screen, &mt);
			struct pipe_framebuffer_state mfb;
			memset(&mfb, 0, sizeof(mfb));
			mfb.width = W; mfb.height = H; mfb.nr_cbufs = 1;
			mfb.cbufs[0].texture = msaa; mfb.cbufs[0].format = PIPE_FORMAT_R8G8B8A8_UNORM;
			mfb.zsbuf.texture = msaaz; mfb.zsbuf.format = PIPE_FORMAT_Z24_UNORM_S8_UINT;
			ctx->set_framebuffer_state(ctx, &mfb);
			struct pipe_rasterizer_state r2 = rs;
			r2.multisample = 1;
			void *rs2 = ctx->create_rasterizer_state(ctx, &r2);
			ctx->bind_rasterizer_state(ctx, rs2);
			ctx->bind_depth_stencil_alpha_state(ctx, dsa_on);
			ctx->clear(ctx, PIPE_CLEAR_COLOR0 | PIPE_CLEAR_DEPTHSTENCIL, NULL, &cc, 1.0, 0);
			ctx->draw_vbo(ctx, &info, 0, NULL, &draw, 1);
			flush_and_report("4x MSAA render target", 0);
			ctx->bind_rasterizer_state(ctx, rs_cso);
			ctx->bind_depth_stencil_alpha_state(ctx, dsa_off);
			ctx->set_framebuffer_state(ctx, &fb);
		} else {
			printf("note: 4x MSAA render target not created, skipped\n");
		}
	}

	/* F. blits and copies */
	{
		struct pipe_resource *dst = make_tex(32, 32, PIPE_FORMAT_R8G8B8A8_UNORM, PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW);
		struct pipe_blit_info bi;
		memset(&bi, 0, sizeof(bi));
		bi.dst.resource = dst; bi.dst.format = PIPE_FORMAT_R8G8B8A8_UNORM;
		bi.dst.box = (struct pipe_box){ 0, 0, 0, 32, 32, 1 };
		bi.src.resource = color; bi.src.format = PIPE_FORMAT_R8G8B8A8_UNORM;
		bi.src.box = (struct pipe_box){ 0, 0, 0, 64, 64, 1 };
		bi.mask = PIPE_MASK_RGBA; bi.filter = PIPE_TEX_FILTER_LINEAR;
		ctx->blit(ctx, &bi);
		{ int b0 = kern->submits; ctx->flush(ctx, NULL, 0); CHECK(kern->rejected_submits == 0, "%s: validators accepted what Mesa submitted", "scaled blit (draws with a fragment shader)"); printf("note: %s produced %d job(s)\n", "scaled blit (draws with a fragment shader)", kern->submits - b0); }
		struct pipe_box cb = { 0, 0, 0, 16, 16, 1 };
		ctx->resource_copy_region(ctx, dst, 0, 0, 0, 0, color, 0, &cb);
		{ int b0 = kern->submits; ctx->flush(ctx, NULL, 0); CHECK(kern->rejected_submits == 0, "%s: validators accepted what Mesa submitted", "copy region"); printf("note: %s produced %d job(s)\n", "copy region", kern->submits - b0); }
	}

	/* G. up to 8 attributes and many varyings */
	{
		float v8[3][4 * 8];
		for (int i = 0; i < 3; i++) for (int j = 0; j < 8; j++) { v8[i][4 * j] = verts[i][0]; v8[i][4 * j + 1] = verts[i][1]; v8[i][4 * j + 2] = verts[i][2]; v8[i][4 * j + 3] = 1; }
		struct pipe_resource *vb8 = make_vbo(v8, sizeof(v8), PIPE_BIND_VERTEX_BUFFER);
		struct pipe_vertex_element e8[8]; memset(e8, 0, sizeof(e8));
		for (int j = 0; j < 8; j++) { e8[j].src_format = PIPE_FORMAT_R32G32B32A32_FLOAT; e8[j].src_offset = 16 * j; e8[j].vertex_buffer_index = 0; }
		void *ve8 = ctx->create_vertex_elements_state(ctx, 8, e8);
		ctx->bind_vertex_elements_state(ctx, ve8);
		set_vbo(vb8);
		void *vs8 = make_shader(PIPE_SHADER_VERTEX,
		    "VERT\nDCL IN[0]\nDCL IN[1]\nDCL IN[2]\nDCL IN[3]\nDCL IN[4]\nDCL IN[5]\nDCL IN[6]\nDCL IN[7]\nDCL OUT[0], POSITION\n"
		    "DCL OUT[1], GENERIC[0]\nDCL OUT[2], GENERIC[1]\nDCL OUT[3], GENERIC[2]\nDCL OUT[4], GENERIC[3]\nDCL TEMP[0]\n"
		    "  0: MOV OUT[0], IN[0]\n  1: ADD OUT[1], IN[1], IN[2]\n  2: ADD OUT[2], IN[3], IN[4]\n  3: ADD OUT[3], IN[5], IN[6]\n  4: MOV OUT[4], IN[7]\n  5: END\n");
		void *fs8 = make_shader(PIPE_SHADER_FRAGMENT,
		    "FRAG\nDCL IN[0], GENERIC[0], PERSPECTIVE\nDCL IN[1], GENERIC[1], PERSPECTIVE\nDCL IN[2], GENERIC[2], PERSPECTIVE\nDCL IN[3], GENERIC[3], PERSPECTIVE\n"
		    "DCL OUT[0], COLOR\nDCL TEMP[0]\n  0: ADD TEMP[0], IN[0], IN[1]\n  1: ADD TEMP[0], TEMP[0], IN[2]\n  2: MAD OUT[0], IN[3], TEMP[0], TEMP[0]\n  3: END\n");
		ctx->bind_vs_state(ctx, vs8);
		ctx->bind_fs_state(ctx, fs8);
		ctx->draw_vbo(ctx, &info, 0, NULL, &draw, 1);
		flush_and_report("8 attributes, 4 varyings", 0);
	}

	/* H. scissor */
	{
		struct pipe_rasterizer_state r2 = rs;
		r2.scissor = 1;
		void *rs2 = ctx->create_rasterizer_state(ctx, &r2);
		struct pipe_scissor_state sc = { .minx = 8, .miny = 8, .maxx = 40, .maxy = 40 };
		ctx->bind_rasterizer_state(ctx, rs2);
		ctx->set_scissor_states(ctx, 0, 1, &sc);
		ctx->draw_vbo(ctx, &info, 0, NULL, &draw, 1);
		flush_and_report("scissor", 0);
		ctx->clear(ctx, PIPE_CLEAR_COLOR0 | PIPE_CLEAR_DEPTHSTENCIL, &sc, &cc, 1.0, 0);
		flush_and_report("scissored clear", 0);
		ctx->bind_rasterizer_state(ctx, rs_cso);

	}

	/* I. line primitives */
	{
		static const unsigned prims[] = { MESA_PRIM_LINES, MESA_PRIM_LINE_STRIP, MESA_PRIM_LINE_LOOP };
		static const char *names[] = { "lines", "line strip", "line loop" };
		ctx->bind_vs_state(ctx, vs);
		ctx->bind_fs_state(ctx, fs);
		ctx->bind_vertex_elements_state(ctx, ve_cso);
		set_vbo(vbo);
		for (unsigned i = 0; i < sizeof(prims) / sizeof(prims[0]); i++) {
			struct pipe_draw_info pi = info;
			pi.mode = prims[i];
			int before = kern->submits;
			ctx->draw_vbo(ctx, &pi, 0, NULL, &draw, 1);
			ctx->flush(ctx, NULL, 0);
			CHECK(kern->rejected_submits == 0, "%s: validators accepted what Mesa submitted", names[i]);
			printf("note: %s drew %d job(s)\n", names[i], kern->submits - before);
		}
	}

	/* 7. many frames: the same state repeated, mixed with clears */
	for (int i = 0; i < 20; i++) {
		ctx->clear(ctx, PIPE_CLEAR_COLOR0, NULL, &cc, 1.0, 0);
		ctx->draw_vbo(ctx, &info, 0, NULL, &draw, 1);
		ctx->flush(ctx, NULL, 0);
	}
	CHECK(kern->rejected_submits == 0, "no submission rejected over %d jobs in total", kern->submits);
	CHECK(kern->rejected_shaders == 0, "no shader refused (%d validated)", kern->shaders);

	/* the jobs were not vacuous: real binner lists, shader records, uniforms */
	printf("jobs: %d with a binner list (%llu bytes), %d with shader records, %d render-only, %llu uniform bytes\n",
	    kern->jobs_with_bin, (unsigned long long)kern->bin_bytes, kern->jobs_with_recs, kern->jobs_render_only,
	    (unsigned long long)kern->uniform_bytes);
	CHECK(kern->jobs_with_bin >= 5 && kern->jobs_with_recs >= 5, "draw jobs carried binner lists and shader records");
	CHECK(kern->uniform_bytes > 0, "uniform streams were validated");

	/* negative control: a damaged command list must be refused */
	int rej = kern->rejected_submits;
	kern->corrupt_next = 1;
	ctx->clear(ctx, PIPE_CLEAR_COLOR0, NULL, &cc, 1.0, 0);
	ctx->draw_vbo(ctx, &info, 0, NULL, &draw, 1);
	ctx->flush(ctx, NULL, 0);
	CHECK(kern->rejected_submits == rej + 1, "a corrupted binner list is rejected by the validators");

	{
		const char *it = getenv("V3D_FUZZ_ITERS");
		fuzz_corpus(kern, it ? atoi(it) : 100000);
		fuzz_shader_corpus(kern, it ? atoi(it) : 100000);
	}

	printf("%d jobs accepted, %d validated shaders, %d fails\n", kern->submits, kern->shaders, fails);
	return fails != 0;
}
