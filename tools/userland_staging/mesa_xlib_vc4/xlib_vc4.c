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
 *
 * The read-back and put run on a presenter thread with its own X
 * connection, so glXSwapBuffers returns once the frame is handed over and
 * the caller builds the next one meanwhile (one frame in flight: the swap
 * after it waits for it). Measured on a Pi 3 (Venice, 1280x720 video): the
 * read is ~6 ms and the put + XSync ~7 ms of every frame, all of it waiting
 * on memory or on the server. Mesa's Xlib frontend has already waited for
 * the GPU (XMesaSwapBuffers' fence_finish) and swaps front and back after
 * this call, so the next frame renders into the other buffer while this
 * one is read; the job holds a reference on the resource so a resize
 * cannot free it under the thread. XLIB_VC4_SYNC=1 presents on the
 * caller's thread as before.
 *
 * Zero-copy (iokit docs/video-decode.md): with a kernel that has
 * kV3DMethodShareBO, each window back buffer's BO is itself the MIT-SHM
 * segment -- the kernel backs the segment with the BO's pages, mapped
 * cacheable in the X server -- so there is no read-back at all: a present
 * is a cache invalidate of the frame (v3d_drm_prepare_read) and the put.
 * One segment per back buffer, kept while the buffer lives (ZC_SLOTS).
 * XLIB_VC4_ZEROCOPY=0 uses the read-back copy instead.
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <pthread.h>
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

static Display *g_display;          /* the application's connection */
static Display *g_pdpy;             /* the connection presents go through */
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
static double g_t_wait, g_t_read, g_t_put, g_t_inflight;
static unsigned g_frames;
static const char *g_path = "";

static double
now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

/* Every 100 frames. "in flight" is how long a swap waited for the frame
 * before it on the presenter thread. */
static void
stats_frame(unsigned w, unsigned h)
{
	if (++g_frames == 100) {
		fprintf(stderr, "xlib_vc4: %ux%u %s, per frame: GPU wait+map %.2f ms, read %.2f ms, put %.2f ms, in flight %.2f ms\n",
		    w, h, g_path, g_t_wait / 100, g_t_read / 100, g_t_put / 100, g_t_inflight / 100);
		g_frames = 0;
		g_t_wait = g_t_read = g_t_put = g_t_inflight = 0;
	}
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

static int
shm_usable(void)
{
	if (g_shm_ok < 0) {
		g_shm_ok = !env_is_1("XLIB_VC4_NOSHM") && XShmQueryExtension(g_pdpy);
		if (!g_shm_ok)
			shm_fail("XShmQueryExtension (or XLIB_VC4_NOSHM)", 0);
	}
	return g_shm_ok;
}

static void
shm_release(void)
{
	if (g_shm.img != NULL) {
		XShmDetach(g_pdpy, &g_shm.info);
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
	if (!shm_usable()) {
		return NULL;
	}
	if (g_shm.img != NULL && g_shm.drawable == xd->drawable &&
	    g_shm.width == width && g_shm.height == height) {
		return g_shm.img;
	}
	shm_release();
	XImage *img = XShmCreateImage(g_pdpy, xd->visual, xd->depth, ZPixmap, NULL,
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
	XShmAttach(g_pdpy, &g_shm.info);
	XSync(g_pdpy, False);
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

/* ---- zero-copy: the back buffer's BO is the MIT-SHM segment ---- */

#define ZC_SLOTS 4
/* The client never attaches the segments: an XImage's data only has to sit
 * at the right distance from shmaddr (XShmPutImage sends the difference). */
#define ZC_BASE ((char *)(uintptr_t)0x10000)

static int g_zc_ok = -1;            /* -1 unknown, 0 off/unsupported, 1 usable */
static struct zc_slot {
	struct pipe_resource *res;  /* referenced: one segment per live buffer */
	uint32_t handle, offset, stride, height;
	Drawable drawable;
	XImage *img;
	XShmSegmentInfo info;
	unsigned long last_use;
} g_zc[ZC_SLOTS];
static unsigned long g_zc_clock;

/* Caller: the presenter is idle (its connection is ours to use). */
static void
zc_drop(struct zc_slot *z)
{
	if (z->img != NULL) {
		XShmDetach(g_pdpy, &z->info);   /* the segment dies with the server's detach */
		z->img->data = NULL;
		XDestroyImage(z->img);
	}
	pipe_resource_reference(&z->res, NULL);
	memset(z, 0, sizeof(*z));
}

static XImage *
zc_image(struct xlib_drawable *xd, struct pipe_resource *res, int fd, uint32_t handle,
    uint32_t offset, unsigned stride, unsigned height)
{
	if (g_zc_ok < 0) {
		const char *e = getenv("XLIB_VC4_ZEROCOPY");
		g_zc_ok = !(e != NULL && strcmp(e, "0") == 0);
	}
	if (!g_zc_ok || !shm_usable()) {
		return NULL;
	}
	struct zc_slot *victim = &g_zc[0];
	for (int i = 0; i < ZC_SLOTS; i++) {
		struct zc_slot *z = &g_zc[i];
		if (z->res == res && z->handle == handle && z->drawable == xd->drawable &&
		    z->offset == offset && z->stride == stride && z->height == height) {
			z->last_use = ++g_zc_clock;
			return z->img;
		}
		if (z->last_use < victim->last_use) {
			victim = z;
		}
	}
	zc_drop(victim);

	/* The image keeps a pointer to its XShmSegmentInfo (image->obdata, read
	 * by every XShmPutImage): it must be the slot's, not a local. */
	XShmSegmentInfo *info = &victim->info;
	XImage *img = XShmCreateImage(g_pdpy, xd->visual, xd->depth, ZPixmap, NULL, info, stride / 4, height);
	if (img == NULL) {
		return NULL;
	}
	if ((unsigned)img->bytes_per_line != stride) {
		XDestroyImage(img);
		memset(victim, 0, sizeof(*victim));
		return NULL;
	}
	info->shmid = shmget(IPC_PRIVATE, (size_t)offset + (size_t)stride * height, IPC_CREAT | 0600);
	if (info->shmid < 0) {
		shm_fail("zero-copy shmget", errno);
		XDestroyImage(img);
		memset(victim, 0, sizeof(*victim));
		return NULL;
	}
	if (v3d_drm_share_bo(fd, handle, info->shmid) != 0) {
		int err = errno;
		shmctl(info->shmid, IPC_RMID, NULL);
		XDestroyImage(img);
		memset(victim, 0, sizeof(*victim));
		if (err == ENOSYS) {
			g_zc_ok = 0;            /* a kernel without kV3DMethodShareBO */
		}
		shm_fail("zero-copy v3d_drm_share_bo", err);
		return NULL;
	}
	info->shmaddr = ZC_BASE;
	info->readOnly = True;
	img->data = ZC_BASE + offset;
	g_shm_error = 0;
	XErrorHandler old = XSetErrorHandler(shm_error_handler);
	XShmAttach(g_pdpy, info);
	XSync(g_pdpy, False);
	XSetErrorHandler(old);
	shmctl(info->shmid, IPC_RMID, NULL);    /* after the attach, see shm_image() */
	if (g_shm_error) {
		shm_fail("zero-copy XShmAttach", 0);
		img->data = NULL;
		XDestroyImage(img);
		memset(victim, 0, sizeof(*victim));
		g_zc_ok = 0;
		return NULL;
	}
	pipe_resource_reference(&victim->res, res);
	victim->handle = handle;
	victim->offset = offset;
	victim->stride = stride;
	victim->height = height;
	victim->drawable = xd->drawable;
	victim->img = img;
	victim->last_use = ++g_zc_clock;
	return img;
}

/* ---- present ---- */

static void
put_boxes(Display *dpy, GC gc, Drawable d, XImage *img, int shm, unsigned w, unsigned h,
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
			XShmPutImage(dpy, d, gc, img, b->x, b->y, b->x, b->y,
			    b->width, b->height, False);
		} else {
			XPutImage(dpy, d, gc, img, b->x, b->y, b->x, b->y,
			    b->width, b->height);
		}
	}
}

/* ---- the presenter thread ---- */

#define MAX_BOXES 16

static struct {
	pthread_mutex_t lock;
	pthread_cond_t cond;
	int started, busy;              /* busy: a job is queued or running */
	/* the job */
	struct pipe_resource *res;      /* referenced until the next swap's wait */
	int fd;
	uint32_t handle, offset, size;
	XImage *img;
	Drawable drawable;
	GC gc;
	unsigned w, h, nboxes;
	struct pipe_box boxes[MAX_BOXES];
	double t_read, t_put;           /* what the thread measured */
	int ok;
	int zc;                         /* img is the BO itself: invalidate, no read */
} g_pr = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER };

static XErrorHandler g_prev_error_handler;

/* Errors on the presenter's connection (a put to a window destroyed
 * meanwhile) are dropped; the rest go to the handler that was there. The
 * handler is process-wide in Xlib. */
static int
present_error_handler(Display *d, XErrorEvent *e)
{
	if (d == g_pdpy) {
		static unsigned logged;
		if (g_stats > 0 && logged++ < 5)
			fprintf(stderr, "xlib_vc4: X error %d (request %d.%d) on the presenter's connection\n",
			    e->error_code, e->request_code, e->minor_code);
		return 0;
	}
	return g_prev_error_handler != NULL ? g_prev_error_handler(d, e) : 0;
}

static void *
presenter_main(void *arg)
{
	(void)arg;
	pthread_mutex_lock(&g_pr.lock);
	for (;;) {
		while (!g_pr.busy) {
			pthread_cond_wait(&g_pr.cond, &g_pr.lock);
		}
		pthread_mutex_unlock(&g_pr.lock);
		double t0 = now_ms();
		if (g_pr.zc) {
			g_pr.ok = v3d_drm_prepare_read(g_pr.fd, g_pr.handle, g_pr.offset, g_pr.size) == 0;
		} else {
			g_pr.ok = v3d_drm_read_bo(g_pr.fd, g_pr.handle, g_pr.offset, g_pr.size, g_pr.img->data) == 0;
		}
		double t1 = now_ms();
		if (g_pr.ok) {
			put_boxes(g_pdpy, g_pr.gc, g_pr.drawable, g_pr.img, 1, g_pr.w, g_pr.h, g_pr.nboxes, g_pr.boxes);
			/* the server must be done with the segment before the next frame */
			XSync(g_pdpy, False);
		}
		g_pr.t_read = t1 - t0;
		g_pr.t_put = now_ms() - t1;
		pthread_mutex_lock(&g_pr.lock);
		g_pr.busy = 0;
		pthread_cond_broadcast(&g_pr.cond);
	}
	return NULL;
}

/* Waits for the frame in flight, then drops its resource reference (on this
 * thread: vc4's resource code is not the presenter's to call). Returns
 * whether the last job's read-back worked. */
static int
presenter_wait(void)
{
	pthread_mutex_lock(&g_pr.lock);
	while (g_pr.busy) {
		pthread_cond_wait(&g_pr.cond, &g_pr.lock);
	}
	pthread_mutex_unlock(&g_pr.lock);
	pipe_resource_reference(&g_pr.res, NULL);
	return g_pr.ok;
}

/* The presenter's own connection and thread, made on first use (the
 * connection on the caller's thread, so Xlib's per-display extension list is
 * only ever grown here). 0 when either is unavailable: present
 * synchronously on the application's connection. */
static int
presenter_start(void)
{
	if (g_pr.started != 0) {
		return g_pr.started > 0;
	}
	g_pr.started = -1;
	if (env_is_1("XLIB_VC4_SYNC")) {
		return 0;
	}
	Display *d = XOpenDisplay(DisplayString(g_display));
	if (d == NULL) {
		return 0;
	}
	g_pdpy = d;
	if (!XShmQueryExtension(d)) {     /* also registers the extension on d */
		g_pdpy = g_display;
		XCloseDisplay(d);
		return 0;
	}
	pthread_t th;
	if (pthread_create(&th, NULL, presenter_main, NULL) != 0) {
		g_pdpy = g_display;
		XCloseDisplay(d);
		return 0;
	}
	pthread_detach(th);
	g_prev_error_handler = XSetErrorHandler(present_error_handler);
	g_pr.ok = 1;
	g_pr.started = 1;
	return 1;
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
	if (g_pdpy == NULL) {
		g_pdpy = g_display;
	}
	int async = presenter_start();
	double tw = g_stats ? now_ms() : 0;
	if (async) {
		/* the frame before must be out of the segment and the buffer */
		presenter_wait();
	}
	double t_inflight = g_stats ? now_ms() - tw : 0;

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
			XFreeGC(g_pdpy, g_gc);
		}
		g_gc = XCreateGC(g_pdpy, xd->drawable, 0, NULL);
		g_gc_drawable = xd->drawable;
	}

	/* 32 bits per pixel only: that is what Mesa's Xlib frontend picks for
	 * TrueColor visuals of depth 24/32, and vc4 renders them natively. */
	struct vc4_resource *rsc = vc4_resource(res);
	unsigned stride = tr->stride;

	if (!rsc->tiled && stride % 4 == 0) {
		uint32_t off = rsc->slices[level].offset + layer * rsc->cube_map_stride;
		struct winsys_handle wh = { .type = WINSYS_HANDLE_TYPE_KMS };
		int fd = vc4_screen(screen)->fd;
		XImage *zimg = screen->resource_get_handle(screen, ctx, res, &wh, 0) ?
		    zc_image(xd, res, fd, wh.handle, off, stride, h) : NULL;
		if (zimg != NULL && async && g_pr.ok && nboxes <= MAX_BOXES) {
			pipe_resource_reference(&g_pr.res, res);
			g_pr.zc = 1;
			g_pr.fd = fd;
			g_pr.handle = wh.handle;
			g_pr.offset = off;
			g_pr.size = stride * h;
			g_pr.img = zimg;
			g_pr.drawable = xd->drawable;
			g_pr.gc = g_gc;
			g_pr.w = w;
			g_pr.h = h;
			g_pr.nboxes = subbox != NULL ? nboxes : 0;
			if (g_pr.nboxes != 0) {
				memcpy(g_pr.boxes, subbox, nboxes * sizeof(*subbox));
			}
			pipe_texture_unmap(ctx, tr);
			pthread_mutex_lock(&g_pr.lock);
			g_pr.busy = 1;
			pthread_cond_signal(&g_pr.cond);
			pthread_mutex_unlock(&g_pr.lock);
			g_path = "linear, zero-copy MIT-SHM, presenter thread";
			if (g_stats) {
				g_t_wait += t1 - t0;
				g_t_inflight += t_inflight;
				g_t_read += g_pr.t_read;
				g_t_put += g_pr.t_put;
				stats_frame(w, h);
			}
			return;
		}
		if (zimg != NULL && v3d_drm_prepare_read(fd, wh.handle, off, stride * h) == 0) {
			double tz = g_stats ? now_ms() : 0;
			put_boxes(g_pdpy, g_gc, xd->drawable, zimg, 1, w, h, nboxes, subbox);
			XSync(g_pdpy, False);
			pipe_texture_unmap(ctx, tr);
			g_pr.ok = 1;            /* a failed invalidate on the thread: try it there again */
			g_path = "linear, zero-copy MIT-SHM";
			if (g_stats) {
				double t3 = now_ms();
				g_t_wait += t1 - t0;
				g_t_read += tz - t1;
				g_t_put += t3 - tz;
				stats_frame(w, h);
			}
			return;
		}
	}

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
		int have_handle = screen->resource_get_handle(screen, ctx, res, &wh, 0);
		if (async && have_handle && g_pr.ok && nboxes <= MAX_BOXES) {
			/* hand the frame to the presenter; GPU rendering is done */
			pipe_resource_reference(&g_pr.res, res);
			g_pr.zc = 0;
			g_pr.fd = vc4_screen(screen)->fd;
			g_pr.handle = wh.handle;
			g_pr.offset = off;
			g_pr.size = stride * h;
			g_pr.img = simg;
			g_pr.drawable = xd->drawable;
			g_pr.gc = g_gc;
			g_pr.w = w;
			g_pr.h = h;
			g_pr.nboxes = subbox != NULL ? nboxes : 0;
			if (g_pr.nboxes != 0) {
				memcpy(g_pr.boxes, subbox, nboxes * sizeof(*subbox));
			}
			pipe_texture_unmap(ctx, tr);
			pthread_mutex_lock(&g_pr.lock);
			g_pr.busy = 1;
			pthread_cond_signal(&g_pr.cond);
			pthread_mutex_unlock(&g_pr.lock);
			g_path = "linear, kernel read, MIT-SHM, presenter thread";
			if (g_stats) {
				/* the thread's times are those of the frame before */
				g_t_wait += t1 - t0;
				g_t_inflight += t_inflight;
				g_t_read += g_pr.t_read;
				g_t_put += g_pr.t_put;
				stats_frame(w, h);
			}
			return;
		}
		if (async && !g_pr.ok) {
			/* the presenter's read failed (an older kernel?): stop using it */
			g_pr.started = -1;
		}
		if (have_handle &&
		    v3d_drm_read_bo(vc4_screen(screen)->fd, wh.handle, off, stride * h, simg->data) == 0) {
			g_path = "linear, kernel read, MIT-SHM";
		} else {
			memcpy(simg->data, map, (size_t)stride * h);   /* uncached read */
			g_path = "linear, mapped read, MIT-SHM";
		}
		t2 = g_stats ? now_ms() : 0;
		put_boxes(g_pdpy, g_gc, xd->drawable, simg, 1, w, h, nboxes, subbox);
		/* the server must be done with the segment before the next frame */
		XSync(g_pdpy, False);
	} else {
		XImage *img = XCreateImage(g_display, xd->visual, xd->depth, ZPixmap, 0, map, w, h, 32, stride);
		t2 = g_stats ? now_ms() : 0;
		if (img != NULL) {
			put_boxes(g_pdpy, g_gc, xd->drawable, img, 0, w, h, nboxes, subbox);
			XFlush(g_pdpy);
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
		stats_frame(w, h);
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
