/*
 * Pixel-diff test for the DAR-437 wrlib fast paths. NOT a standalone test:
 * build_wrlib_fastpath_test.sh compiles this file three times --
 *   ref    : against PRISTINE upstream wrlib/{convert,raster,gradient}.c
 *            (git show wmaker-0.96.0:...), i.e. the "current code"
 *   new    : against the working tree (fast paths, NEON if the host has it)
 *   newsc  : against the working tree with -DWR_NO_NEON (scalar fast paths)
 * -- each binary writes every produced buffer (XImage data INCLUDING its
 * row padding, RImage data) to a file, and the script cmp's the files.
 * convert.c/raster.c/gradient.c are #included so the static
 * image2TrueColor is reachable without an X server; only the two XImage
 * helpers from xutil.c are stubbed, and XPutPixel's f.put_pixel is a
 * transcription of libX11 src/ImUtil.c:727-750 (_XPutPixel32) plus a 16bpp
 * twin, so the reference really pays the per-pixel function-pointer cost.
 */
#include "convert.c"
#include "alpha_combine.c"
#include "raster.c"
#include "gradient.c"

int RErrorCode;

static FILE *out;
static unsigned rng_state = 12345;
static unsigned rnd(void) { rng_state = rng_state * 1664525u + 1013904223u; return rng_state >> 8; }

static int host_lsb(void) { unsigned one = 1; return *(unsigned char *)&one == 1; }

static int put_pixel(XImage *im, int x, int y, unsigned long pixel)
{
	unsigned char *addr;
	if (im->bits_per_pixel == 32) {
		addr = (unsigned char *)im->data + y * im->bytes_per_line + (x << 2);
		if (host_lsb() == (im->byte_order == LSBFirst))
			*((unsigned int *)addr) = (unsigned int)pixel;
		else if (im->byte_order == MSBFirst) {
			addr[0] = pixel >> 24; addr[1] = pixel >> 16; addr[2] = pixel >> 8; addr[3] = pixel;
		} else {
			addr[3] = pixel >> 24; addr[2] = pixel >> 16; addr[1] = pixel >> 8; addr[0] = pixel;
		}
	} else {
		addr = (unsigned char *)im->data + y * im->bytes_per_line + (x << 1);
		if (im->byte_order == MSBFirst) { addr[0] = pixel >> 8; addr[1] = pixel; }
		else { addr[1] = pixel >> 8; addr[0] = pixel; }
	}
	return 1;
}

static int g_bpl_extra, g_bo, g_bpp;

RXImage *RCreateXImage(RContext *context, int depth, unsigned width, unsigned height)
{
	RXImage *r = calloc(1, sizeof(RXImage));
	XImage *im = calloc(1, sizeof(XImage));
	int bytes = g_bpp / 8;
	im->width = width; im->height = height; im->format = ZPixmap;
	im->bits_per_pixel = g_bpp; im->depth = depth; im->byte_order = g_bo;
	im->bytes_per_line = width * bytes + g_bpl_extra;
	im->data = malloc(im->bytes_per_line * height);
	memset(im->data, 0xA5, im->bytes_per_line * height);	/* padding canary */
	im->f.put_pixel = put_pixel;
	r->image = im;
	return r;
}

void RDestroyXImage(RContext *context, RXImage *ximage)
{
	free(ximage->image->data); free(ximage->image); free(ximage);
}

static void dump(const void *p, size_t n)
{
	unsigned int hdr = (unsigned int)n;
	fwrite(&hdr, 4, 1, out);
	fwrite(p, 1, n, out);
}

static RImage *mkimg(unsigned w, unsigned h, int alpha)
{
	RImage *im = RCreateImage(w, h, alpha);
	size_t i, n = (size_t)w * h * (alpha ? 4 : 3);
	for (i = 0; i < n; i++) {
		unsigned v = rnd();
		im->data[i] = (v % 11 == 0) ? 0 : (v % 11 == 1) ? 255 : v;	/* hit 0/255 often */
	}
	return im;
}

static const unsigned widths[] = {1, 2, 3, 15, 16, 17, 31, 32, 33, 64, 101, 257};
static const unsigned heights[] = {1, 3, 8};
#define NW (sizeof(widths) / sizeof(widths[0]))
#define NH (sizeof(heights) / sizeof(heights[0]))

static void test_convert(void)
{
	static const int lay[][3] = {{16, 8, 0}, {0, 8, 16}, {24, 16, 8}, {8, 16, 24}, {4, 12, 20}, {16, 0, 8}};
	static const int bpls[] = {0, 4, 12};
	RContext ctx;
	RContextAttributes attr;
	Visual vis;
	unsigned l, bo, mode, wi, hi, ch, bx;

	memset(&ctx, 0, sizeof ctx);
	memset(&vis, 0, sizeof vis);
	ctx.attribs = &attr; ctx.visual = &vis; ctx.vclass = TrueColor; ctx.depth = 24;
	for (l = 0; l < sizeof(lay) / sizeof(lay[0]); l++)
	for (bo = 0; bo < 2; bo++)
	for (mode = 0; mode < 2; mode++)
	for (ch = 3; ch <= 4; ch++)
	for (bx = 0; bx < 3; bx++)
	for (wi = 0; wi < NW; wi++)
	for (hi = 0; hi < NH; hi++) {
		RImage *im = mkimg(widths[wi], heights[hi], ch == 4);
		RXImage *x;
		memset(&attr, 0, sizeof attr);
		attr.render_mode = mode ? RBestMatchRendering : RDitheredRendering;
		vis.red_mask = 0xffUL << lay[l][0]; vis.green_mask = 0xffUL << lay[l][1]; vis.blue_mask = 0xffUL << lay[l][2];
		ctx.red_offset = lay[l][0]; ctx.green_offset = lay[l][1]; ctx.blue_offset = lay[l][2];
		g_bo = bo ? MSBFirst : LSBFirst; g_bpp = 32; g_bpl_extra = bpls[bx];
		x = image2TrueColor(&ctx, im);
		dump(x->image->data, (size_t)x->image->bytes_per_line * im->height);
		RDestroyXImage(&ctx, x);
		RReleaseImage(im);
	}
	/* 565 / 16bpp: generic (non-0xff) path must be untouched */
	for (bo = 0; bo < 2; bo++)
	for (mode = 0; mode < 2; mode++)
	for (ch = 3; ch <= 4; ch++)
	for (wi = 0; wi < NW; wi++) {
		RImage *im = mkimg(widths[wi], 5, ch == 4);
		RXImage *x;
		memset(&attr, 0, sizeof attr);
		attr.render_mode = mode ? RBestMatchRendering : RDitheredRendering;
		vis.red_mask = 0xf800; vis.green_mask = 0x07e0; vis.blue_mask = 0x001f;
		ctx.red_offset = 11; ctx.green_offset = 5; ctx.blue_offset = 0; ctx.depth = 16;
		g_bo = bo ? MSBFirst : LSBFirst; g_bpp = 16; g_bpl_extra = 2;
		x = image2TrueColor(&ctx, im);
		dump(x->image->data, (size_t)x->image->bytes_per_line * im->height);
		RDestroyXImage(&ctx, x);
		RReleaseImage(im);
	}
	ctx.depth = 24;
}

static void test_gradient(void)
{
	static const RColor cols[][2] = {{{200, 40, 40, 255}, {30, 60, 220, 255}}, {{0, 0, 0, 255}, {255, 255, 255, 255}},
					 {{255, 7, 128, 255}, {0, 250, 3, 255}}};
	unsigned c, st, wi, hi;
	for (c = 0; c < 3; c++)
	for (st = 0; st < 3; st++)
	for (wi = 0; wi < NW; wi++)
	for (hi = 0; hi < NH; hi++) {
		RImage *im = RRenderGradient(widths[wi], heights[hi], &cols[c][0], &cols[c][1],
					     st == 0 ? RHorizontalGradient : st == 1 ? RVerticalGradient : RDiagonalGradient);
		dump(im->data, (size_t)im->width * im->height * 3);
		RReleaseImage(im);
	}
}

static void test_combine(void)
{
	static const int ops[] = {0, 1, 127, 128, 254, 255};
	unsigned da, sa, wi, hi, o;
	for (da = 0; da < 2; da++)
	for (sa = 0; sa < 2; sa++)
	for (wi = 0; wi < NW; wi++)
	for (hi = 0; hi < NH; hi++) {
		RImage *d = mkimg(widths[wi], heights[hi], da), *s = mkimg(widths[wi], heights[hi], sa);
		RCombineImages(d, s);
		dump(d->data, (size_t)d->width * d->height * (da ? 4 : 3));
		for (o = 0; o < sizeof(ops) / sizeof(ops[0]); o++) {
			RImage *d2 = mkimg(widths[wi], heights[hi], da);
			RCombineImagesWithOpaqueness(d2, s, ops[o]);
			dump(d2->data, (size_t)d2->width * d2->height * (da ? 4 : 3));
			RReleaseImage(d2);
		}
		RReleaseImage(d); RReleaseImage(s);
	}
	/* RCombineArea: clipped/offset rectangles */
	for (da = 0; da < 2; da++)
	for (sa = 0; sa < 2; sa++) {
		static const int geo[][6] = {{0, 0, 40, 29, 0, 0}, {3, 2, 37, 20, 5, 4}, {0, 0, 33, 9, -4, -3},
					     {1, 1, 17, 5, 70, 10}, {0, 0, 16, 16, 10, 10}, {5, 0, 50, 3, 60, 27}};
		unsigned g;
		for (g = 0; g < sizeof(geo) / sizeof(geo[0]); g++) {
			RImage *d = mkimg(97, 31, da), *s = mkimg(61, 29, sa);
			RCombineArea(d, s, geo[g][0], geo[g][1], geo[g][2], geo[g][3], geo[g][4], geo[g][5]);
			dump(d->data, (size_t)d->width * d->height * (da ? 4 : 3));
			RReleaseImage(d); RReleaseImage(s);
		}
	}
}

int main(int argc, char **argv)
{
	if (argc < 2 || !(out = fopen(argv[1], "wb"))) { fprintf(stderr, "usage: %s outfile\n", argv[0]); return 2; }
#if defined(WR_REF)
	puts("variant: reference (pristine upstream)");
#elif defined(WR_NEON)
	puts("variant: NEON fast path");
#else
	puts("variant: scalar fast path");
#endif
	test_convert();
	test_gradient();
	test_combine();
	fclose(out);
	puts("done");
	return 0;
}
