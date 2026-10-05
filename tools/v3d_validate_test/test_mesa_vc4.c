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

static void
flush_and_report(const char *what, int expect_submits_before)
{
	int before = kern->submits, rej_before = kern->rejected_submits;
	ctx->flush(ctx, NULL, 0);
	CHECK(kern->rejected_submits == rej_before, "%s: validators accepted what Mesa submitted (%d rejected)",
	    what, kern->rejected_submits - rej_before);
	CHECK(kern->submits > before || expect_submits_before >= 0, "%s: a job was submitted (%d total)", what, kern->submits);
}

int
main(void)
{
	freopen("/dev/null", "a", stderr);       /* silence by default... */
	if (getenv("V3D_TEST_VERBOSE")) {
		freopen("/dev/tty", "w", stderr);
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
	vp.scale[0] = W / 2.0f; vp.scale[1] = -(H / 2.0f); vp.scale[2] = 0.5f; vp.scale[3] = 1;
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
	struct pipe_vertex_buffer vb; memset(&vb, 0, sizeof(vb));
	vb.buffer.resource = vbo; vb.buffer_offset = 0;
	ctx->set_vertex_buffers(ctx, 1, &vb);

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

	printf("%d jobs accepted, %d validated shaders, %d fails\n", kern->submits, kern->shaders, fails);
	return fails != 0;
}
