/*
 * Hardware-capable replacement for Mesa's src/gallium/targets/libgl-xlib/
 * xlib.c (docs/gpu-opengl-plan.md, Tier C). That file's single job is
 * xlib_create_screen(Display *): build the pipe_screen the Xlib GLX frontend
 * renders with. The stock one always builds softpipe on a sw_winsys that
 * puts frames on the X drawable.
 *
 * This one first tries the VideoCore IV: open the BCM2837V3D service through
 * the libdrm shim and create Mesa's vc4 screen on it. If there is no V3D
 * (QEMU, a driver that declined, MESA_VC4=0) it falls back to the stock
 * software path, so one libGL serves both. Linked in place of xlib.c by
 * build_mesa.sh (MESA_VC4=1).
 *
 * Presenting (the X server is 2D-only, so every frame is copied by the CPU).
 * Measured on a Pi 3 (glxgears, docs/v3d-bringup.md): the GPU is idle long
 * before the copy ends; what costs is (1) CPU reads of GPU memory -- BO user
 * mappings are write-combined, reads through them are uncached, 49 MB/s
 * against 330 MB/s for ordinary memory -- and (2) XPutImage, which pushes
 * every pixel through the X socket (about 25-50 MB/s end to end). So:
 *   - window back buffers are rendered linear (no untile on the CPU);
 *   - the frame is read with v3d_drm_read_bo(), which the kernel does
 *     through a cached mapping after a cache flush, straight into
 *   - an MIT-SHM image, put with XShmPutImage (no socket copy).
 * Each step falls back to the slow way when it is unavailable (no MIT-SHM
 * on the display, an older kernel). XLIB_VC4_TILED=1 renders tiled again
 * and XLIB_VC4_NOSHM=1 disables MIT-SHM (comparisons); XLIB_VC4_STATS=1
 * prints the per-frame cost of each step every 100 frames.
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "util/compiler.h"
#include "util/u_debug.h"
#include "util/u_inlines.h"
#include "util/os_time.h"
#include "frontend/xlibsw_api.h"
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "sw/xlib/xlib_sw_winsys.h"

#include "target-helpers/inline_sw_helper.h"
#include "target-helpers/inline_debug_helper.h"

#include "vc4/vc4_screen.h"
#include "vc4/vc4_resource.h"
#include "frontend/winsys_handle.h"
#include "v3d_drm.h"

struct pipe_screen *xlib_create_screen(Display *display);

static Display *g_display;
static struct pipe_context *g_present_ctx;     /* only for callers that pass no context */
static Drawable g_gc_drawable;
static GC g_gc;

static int
env_is_1(const char *name)
{
	const char *s = getenv(name);
	return s != NULL && strcmp(s, "1") == 0;
}

/* ---- XLIB_VC4_STATS ---- */

static int g_stats = -1;
static double g_t_wait, g_t_read, g_t_put;
static unsigned g_frames;
static const char *g_path = "";

static double
now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

/* ---- MIT-SHM image, one per window (re-made when the window resizes) ---- */

static int g_shm_ok = -1;          /* -1 unknown, 0 unavailable, 1 usable */
static struct {
	Drawable drawable;
	unsigned width, height;    /* image size in pixels (width = row pitch / 4) */
	XImage *img;
	XShmSegmentInfo info;
} g_shm;
static int g_shm_error;

/* Why MIT-SHM was not used (XLIB_VC4_STATS=1 prints it once). */
static void
shm_fail(const char *step, int err)
{
	if (getenv("XLIB_VC4_STATS") != NULL)
		fprintf(stderr, "xlib_vc4: MIT-SHM not used: %s failed (%s)\n", step, err ? strerror(err) : "-");
}

static int
shm_error_handler(Display *d, XErrorEvent *e)
{
	(void)d;
	(void)e;
	g_shm_error = 1;
	return 0;
}

static void
shm_release(void)
{
	if (g_shm.img != NULL) {
		XShmDetach(g_display, &g_shm.info);
		g_shm.img->data = NULL;
		XDestroyImage(g_shm.img);
		shmdt(g_shm.info.shmaddr);
	}
	memset(&g_shm, 0, sizeof(g_shm));
}

/* An MIT-SHM image `width` x `height` for this window, or NULL. */
static XImage *
shm_image(struct xlib_drawable *xd, unsigned width, unsigned height)
{
	if (g_shm_ok < 0) {
		g_shm_ok = !env_is_1("XLIB_VC4_NOSHM") && XShmQueryExtension(g_display);
		if (!g_shm_ok)
			shm_fail("XShmQueryExtension (or XLIB_VC4_NOSHM)", 0);
	}
	if (!g_shm_ok) {
		return NULL;
	}
	if (g_shm.img != NULL && g_shm.drawable == xd->drawable &&
	    g_shm.width == width && g_shm.height == height) {
		return g_shm.img;
	}
	shm_release();
	XImage *img = XShmCreateImage(g_display, xd->visual, xd->depth, ZPixmap, NULL,
	    &g_shm.info, width, height);
	if (img == NULL) {
		shm_fail("XShmCreateImage", 0);
		g_shm_ok = 0;
		return NULL;
	}
	g_shm.info.shmid = shmget(IPC_PRIVATE, (size_t)img->bytes_per_line * height, IPC_CREAT | 0600);
	if (g_shm.info.shmid < 0) {
		/* Usually the SysV limits (xnu defaults: shmmax 4 MiB, shmall 1024
		 * pages, bsd/kern/sysv_shm.c:166-170) for a big window: this size
		 * falls back, a smaller window may still get MIT-SHM. */
		shm_fail("shmget", errno);
		XDestroyImage(img);
		return NULL;
	}
	g_shm.info.shmaddr = img->data = shmat(g_shm.info.shmid, NULL, 0);
	if (g_shm.info.shmaddr == (char *)-1) {
		shm_fail("shmat", errno);
		shmctl(g_shm.info.shmid, IPC_RMID, NULL);
		img->data = NULL;
		XDestroyImage(img);
		return NULL;
	}
	g_shm.info.readOnly = True;
	/* A display on another machine refuses the attach: fall back. */
	g_shm_error = 0;
	XErrorHandler old = XSetErrorHandler(shm_error_handler);
	XShmAttach(g_display, &g_shm.info);
	XSync(g_display, False);
	XSetErrorHandler(old);
	/* Remove only now: unlike Linux, xnu refuses to find a segment marked
	 * IPC_RMID (shm_find_segment_by_shmid, bsd/kern/sysv_shm.c:260-264), so
	 * the server's attach failed when this came right after our shmat. It
	 * is freed once both sides detach. */
	shmctl(g_shm.info.shmid, IPC_RMID, NULL);
	if (g_shm_error) {
		shm_fail("XShmAttach", 0);
		img->data = NULL;
		XDestroyImage(img);
		shmdt(g_shm.info.shmaddr);
		memset(&g_shm, 0, sizeof(g_shm));
		g_shm_ok = 0;
		return NULL;
	}
	g_shm.img = img;
	g_shm.drawable = xd->drawable;
	g_shm.width = width;
	g_shm.height = height;
	return img;
}

/* ---- present ---- */

static void
put_boxes(struct xlib_drawable *xd, XImage *img, int shm, unsigned w, unsigned h,
    unsigned nboxes, const struct pipe_box *subbox)
{
	struct pipe_box whole = { .x = 0, .y = 0, .width = (int)w, .height = (int)h };
	if (nboxes == 0 || subbox == NULL) {
		nboxes = 1;
		subbox = &whole;
	}
	for (unsigned i = 0; i < nboxes; i++) {
		const struct pipe_box *b = &subbox[i];
		if (shm) {
			XShmPutImage(g_display, xd->drawable, g_gc, img, b->x, b->y, b->x, b->y,
			    b->width, b->height, False);
		} else {
			XPutImage(g_display, xd->drawable, g_gc, img, b->x, b->y, b->x, b->y,
			    b->width, b->height);
		}
	}
}

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
	if (g_stats < 0) {
		g_stats = env_is_1("XLIB_VC4_STATS");
	}

	unsigned w = u_minify(res->width0, level), h = u_minify(res->height0, level);
	double t0 = g_stats ? now_ms() : 0;
	/* Flushes the jobs that render into `res` and waits for them; for a
	 * linear resource it is a direct mapping (no copy). */
	struct pipe_transfer *tr = NULL;
	void *map = pipe_texture_map(ctx, res, level, layer, PIPE_MAP_READ, 0, 0, w, h, &tr);
	if (map == NULL) {
		return;
	}
	double t1 = g_stats ? now_ms() : 0;

	if (g_gc_drawable != xd->drawable || g_gc == NULL) {
		if (g_gc != NULL) {
			XFreeGC(g_display, g_gc);
		}
		g_gc = XCreateGC(g_display, xd->drawable, 0, NULL);
		g_gc_drawable = xd->drawable;
	}

	/* 32 bits per pixel only: that is what Mesa's Xlib frontend picks for
	 * TrueColor visuals of depth 24/32, and vc4 renders them natively. */
	struct vc4_resource *rsc = vc4_resource(res);
	unsigned stride = tr->stride;
	XImage *simg = (!rsc->tiled && stride % 4 == 0) ? shm_image(xd, stride / 4, h) : NULL;
	double t2;
	if (simg != NULL && (unsigned)simg->bytes_per_line != stride && g_stats)
		fprintf(stderr, "xlib_vc4: MIT-SHM not used: row pitch %u, image %d\n", stride, simg->bytes_per_line);
	if (simg != NULL && (unsigned)simg->bytes_per_line == stride) {
		/* the frame is rows [0, h) of the linear slice */
		uint32_t off = rsc->slices[level].offset + layer * rsc->cube_map_stride;
		/* The BO handle through the generic gallium call (vc4_bufmgr.h,
		 * which defines struct vc4_bo, drags in compiler headers). It marks
		 * the BO shared, so vc4 does not recycle it through its BO cache. */
		struct winsys_handle wh = { .type = WINSYS_HANDLE_TYPE_KMS };
		if (screen->resource_get_handle(screen, ctx, res, &wh, 0) &&
		    v3d_drm_read_bo(vc4_screen(screen)->fd, wh.handle, off, stride * h, simg->data) == 0) {
			g_path = "linear, kernel read, MIT-SHM";
		} else {
			memcpy(simg->data, map, (size_t)stride * h);   /* uncached read */
			g_path = "linear, mapped read, MIT-SHM";
		}
		t2 = g_stats ? now_ms() : 0;
		put_boxes(xd, simg, 1, w, h, nboxes, subbox);
		/* the server must be done with the segment before the next frame */
		XSync(g_display, False);
	} else {
		XImage *img = XCreateImage(g_display, xd->visual, xd->depth, ZPixmap, 0, map, w, h, 32, stride);
		t2 = g_stats ? now_ms() : 0;
		if (img != NULL) {
			put_boxes(xd, img, 0, w, h, nboxes, subbox);
			XFlush(g_display);
			img->data = NULL;                 /* the mapping is not ours to free */
			XDestroyImage(img);
		}
		g_path = rsc->tiled ? "tiled, XPutImage" : "linear, XPutImage";
	}
	pipe_texture_unmap(ctx, tr);

	if (g_stats) {
		double t3 = now_ms();
		g_t_wait += t1 - t0;
		g_t_read += t2 - t1;
		g_t_put += t3 - t2;
		if (++g_frames == 100) {
			fprintf(stderr, "xlib_vc4: %ux%u %s, per frame: GPU wait+map %.2f ms, read %.2f ms, put %.2f ms\n",
			    w, h, g_path, g_t_wait / 100, g_t_read / 100, g_t_put / 100);
			g_frames = 0;
			g_t_wait = g_t_read = g_t_put = 0;
		}
	}
}

/* Window back buffers (PIPE_BIND_DISPLAY_TARGET) are created linear unless
 * XLIB_VC4_TILED=1: presenting then needs no untile. vc4 renders linear
 * targets natively (vc4_resource.c: PIPE_BIND_LINEAR -> rsc->tiled =
 * false; the RCL stores them with VC4_TILING_FORMAT_LINEAR). */
static struct pipe_resource *(*g_orig_resource_create)(struct pipe_screen *, const struct pipe_resource *);

static struct pipe_resource *
xlib_vc4_resource_create(struct pipe_screen *screen, const struct pipe_resource *tmpl)
{
	if (tmpl->bind & PIPE_BIND_DISPLAY_TARGET) {
		struct pipe_resource t = *tmpl;
		t.bind |= PIPE_BIND_LINEAR;
		return g_orig_resource_create(screen, &t);
	}
	return g_orig_resource_create(screen, tmpl);
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
	if (!env_is_1("XLIB_VC4_TILED")) {
		g_orig_resource_create = screen->resource_create;
		screen->resource_create = xlib_vc4_resource_create;
	}
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
