/*
 * Hardware-capable replacement for Mesa's src/gallium/targets/libgl-xlib/
 * xlib.c (docs/gpu-opengl-plan.md, Tier C). That file's single job is
 * xlib_create_screen(Display *): build the pipe_screen the Xlib GLX frontend
 * renders with. The stock one always builds softpipe on a sw_winsys that
 * puts frames on the X drawable.
 *
 * This one first tries the VideoCore IV: open the BCM2837V3D service through
 * the libdrm shim and create Mesa's vc4 screen on it. Frames rendered by the
 * GPU are copied to the X drawable by xlib_vc4_flush_frontbuffer() below
 * (map the render target -- Mesa's vc4 transfer path untiles it -- and
 * XPutImage; the X server here is 2D-only). If there is no V3D (QEMU, a
 * driver that declined, MESA_VC4=0) it falls back to the stock software
 * path, so one libGL serves both.
 *
 * Untested on hardware. Linked in place of xlib.c by build_mesa_vc4.sh.
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdlib.h>
#include <string.h>

#include "util/compiler.h"
#include "util/u_debug.h"
#include "util/u_inlines.h"
#include "frontend/xlibsw_api.h"
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "sw/xlib/xlib_sw_winsys.h"

#include "target-helpers/inline_sw_helper.h"
#include "target-helpers/inline_debug_helper.h"

#include "vc4/vc4_screen.h"
#include "v3d_drm.h"

struct pipe_screen *xlib_create_screen(Display *display);

static Display *g_display;
static struct pipe_context *g_present_ctx;     /* only for callers that pass no context */
static Drawable g_gc_drawable;
static GC g_gc;

static void
xlib_vc4_flush_frontbuffer(struct pipe_screen *screen, struct pipe_context *ctx,
                           struct pipe_resource *res, unsigned level, unsigned layer,
                           void *winsys_drawable_handle, unsigned nboxes, struct pipe_box *subbox)
{
	struct xlib_drawable *xd = winsys_drawable_handle;
	if (xd == NULL || g_display == NULL || res == NULL) {
		return;
	}
	if (ctx == NULL) {
		if (g_present_ctx == NULL) {
			g_present_ctx = screen->context_create(screen, NULL, 0);
		}
		ctx = g_present_ctx;
		if (ctx == NULL) {
			return;
		}
	}

	unsigned w = u_minify(res->width0, level), h = u_minify(res->height0, level);
	struct pipe_transfer *tr = NULL;
	/* Waits for the GPU job that renders into `res` (vc4 flushes the jobs
	 * writing the resource on the context it is given). */
	void *map = pipe_texture_map(ctx, res, level, layer, PIPE_MAP_READ, 0, 0, w, h, &tr);
	if (map == NULL) {
		return;
	}

	if (g_gc_drawable != xd->drawable || g_gc == NULL) {
		if (g_gc != NULL) {
			XFreeGC(g_display, g_gc);
		}
		g_gc = XCreateGC(g_display, xd->drawable, 0, NULL);
		g_gc_drawable = xd->drawable;
	}

	/* 32 bits per pixel only: that is what Mesa's Xlib frontend picks for
	 * TrueColor visuals of depth 24/32, and vc4 renders them natively. */
	XImage *img = XCreateImage(g_display, xd->visual, xd->depth, ZPixmap, 0, map, w, h, 32, tr->stride);
	if (img != NULL) {
		if (nboxes > 0 && subbox != NULL) {
			for (unsigned i = 0; i < nboxes; i++) {
				XPutImage(g_display, xd->drawable, g_gc, img, subbox[i].x, subbox[i].y,
				    subbox[i].x, subbox[i].y, subbox[i].width, subbox[i].height);
			}
		} else {
			XPutImage(g_display, xd->drawable, g_gc, img, 0, 0, 0, 0, w, h);
		}
		XFlush(g_display);
		img->data = NULL;                 /* the mapping is not ours to free */
		XDestroyImage(img);
	}
	pipe_texture_unmap(ctx, tr);
}

static struct pipe_screen *
create_hardware_screen(void)
{
	const char *opt = getenv("MESA_VC4");
	if (opt != NULL && strcmp(opt, "0") == 0) {
		return NULL;
	}
	int fd = v3d_drm_open();
	if (fd < 0) {
		return NULL;                      /* no V3D: the caller falls back to software */
	}
	struct pipe_screen *screen = vc4_screen_create(fd, NULL, NULL);
	if (screen == NULL) {
		v3d_drm_close(fd);
		return NULL;
	}
	screen->flush_frontbuffer = xlib_vc4_flush_frontbuffer;
	return screen;
}

struct pipe_screen *
xlib_create_screen(Display *display)
{
	struct sw_winsys *winsys;
	struct pipe_screen *screen;

	g_display = display;

	screen = create_hardware_screen();
	if (screen != NULL) {
		return debug_screen_wrap(screen);
	}

	/* stock software path, as in targets/libgl-xlib/xlib.c */
	winsys = xlib_create_sw_winsys(display);
	if (winsys == NULL) {
		return NULL;
	}
	screen = sw_screen_create(winsys);
	if (screen == NULL) {
		winsys->destroy(winsys);
		return NULL;
	}
	return debug_screen_wrap(screen);
}
