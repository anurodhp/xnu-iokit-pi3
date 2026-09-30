/*
 * wrlib / Xlib micro-benchmark (DAR-438). Times the CPU-bound paths Window
 * Maker hits on every window/menu/dock redraw, so the same binary can be run
 * against an -O0 and an -O2 build of the X11 stack on the same image:
 *   1. wrlib (no display): RRenderGradient, RScaleImage, RSmoothScaleImage,
 *      RCombineImages, RBevelImage, RBlurImage.
 *   2. wrlib + Xlib + server (display argv[1], default ":0"): RConvertImage
 *      (renders to a Pixmap, XPutImage to the server), then 2000
 *      XFillRectangle/XDrawLine/XDrawString + XSync round trips, and 100
 *      XGetImage of a 256x256 area.
 * Wall-clock via gettimeofday; under QEMU TCG the numbers are only
 * indicative. Usage: x11_bench [display] [iterations]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include "wraster.h"

static double now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static int iters = 5;
#define TIME(name, n, body) do { double t0 = now_ms(); int k_; \
    for (k_ = 0; k_ < (n); k_++) { body; } \
    printf("BENCH %-28s %4d x %9.2f ms total  %8.3f ms/op\n", name, (n), \
           now_ms() - t0, (now_ms() - t0) / (n)); fflush(stdout); } while (0)

int main(int argc, char **argv)
{
    const char *dpyname = argc > 1 ? argv[1] : ":0";
    RColor c1 = {200, 40, 40, 255}, c2 = {30, 60, 220, 255};
    RImage *a, *b, *t;
    Display *dpy;
    int i;

    if (argc > 2) iters = atoi(argv[2]);
    a = RRenderGradient(512, 384, &c1, &c2, RGRD_DIAGONAL);
    b = RRenderGradient(512, 384, &c2, &c1, RGRD_HORIZONTAL);
    if (!a || !b) { puts("FAIL: gradient"); return 1; }

    TIME("wr gradient diag 512x384", iters, { t = RRenderGradient(512, 384, &c1, &c2, RGRD_DIAGONAL); RReleaseImage(t); });
    TIME("wr gradient horiz 512x384", iters, { t = RRenderGradient(512, 384, &c1, &c2, RGRD_HORIZONTAL); RReleaseImage(t); });
    TIME("wr RScaleImage ->640x480", iters, { t = RScaleImage(a, 640, 480); RReleaseImage(t); });
    {   /* DAR-437: same op on an image no gradient code touched, to separate data/state effects */
        RImage *n = RCreateImage(512, 384, False);
        int k;
        for (k = 0; k < 512 * 384 * 3; k++) n->data[k] = (unsigned char)(k * 5 + (k >> 7));
        TIME("wr RScaleImage (noise src)", iters, { t = RScaleImage(n, 640, 480); RReleaseImage(t); });
        RReleaseImage(n);
        TIME("wr RScaleImage (grad src)", iters, { t = RScaleImage(a, 640, 480); RReleaseImage(t); });
    }
    TIME("wr RSmoothScaleImage ->320x240", iters, { t = RSmoothScaleImage(a, 320, 240); RReleaseImage(t); });
    t = RCloneImage(a);
    TIME("wr RCombineImages 512x384", iters, RCombineImagesWithOpaqueness(t, b, 128));
    TIME("wr RBevelImage 512x384", iters, RBevelImage(t, RBEV_RAISED2));
    TIME("wr RBlurImage 512x384", iters, RBlurImage(t));
    RReleaseImage(t);
    {   /* DAR-437: RGBA source over an RGB destination (RCombineImages alpha path) */
        RImage *s = RCreateImage(512, 384, True);
        int k;
        if (!s) { puts("FAIL: rgba"); return 1; }
        for (k = 0; k < 512 * 384 * 4; k++) s->data[k] = (unsigned char)(k * 7 + (k >> 9));
        t = RCloneImage(a);
        TIME("wr RCombineImages RGBA src", iters, RCombineImages(t, s));
        RReleaseImage(t); RReleaseImage(s);
    }

    dpy = XOpenDisplay(dpyname);
    if (!dpy) { printf("NOX no display %s; CPU-only results above\n", dpyname); puts("BENCHDONE"); return 0; }
    {
        int scr = DefaultScreen(dpy);
        RContext *rc = RCreateContext(dpy, scr, NULL);
        Window w = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 0, 0, 512, 384, 0, 0, 0);
        GC gc = XCreateGC(dpy, w, 0, NULL);
        XImage *xi;
        Pixmap pm;
        XMapWindow(dpy, w);
        XSync(dpy, False);
        if (!rc) { puts("FAIL: RCreateContext"); return 1; }
        {   /* DAR-437: RConvertImage output must equal the source pixels packed by the visual's masks */
            XImage *ci;
            int x, y, bad = 0;
            if (!RConvertImage(rc, a, &pm)) { puts("FAIL: convert"); return 1; }
            ci = XGetImage(dpy, pm, 0, 0, 512, 384, AllPlanes, ZPixmap);
            if (!ci) { puts("FAIL: getimage pm"); return 1; }
            if (rc->depth >= 24)
                for (y = 0; y < 384; y++)
                    for (x = 0; x < 512; x++) {
                        unsigned char *p = a->data + (y * 512 + x) * 3;
                        unsigned long e = ((unsigned long)p[0] << rc->red_offset) | ((unsigned long)p[1] << rc->green_offset) | ((unsigned long)p[2] << rc->blue_offset);
                        if ((XGetPixel(ci, x, y) & 0xffffff) != e) bad++;
                    }
            printf("CONVCHECK depth=%d bpp=%d byte_order=%d offs=%d,%d,%d mismatches=%d %s\n", rc->depth, ci->bits_per_pixel,
                   ci->byte_order, rc->red_offset, rc->green_offset, rc->blue_offset, bad, bad ? "FAIL" : "OK");
            XDestroyImage(ci); XFreePixmap(dpy, pm);
        }
        TIME("wr RConvertImage+XFreePixmap", iters, { if (!RConvertImage(rc, a, &pm)) { puts("FAIL: convert"); return 1; } XCopyArea(dpy, pm, w, gc, 0, 0, 512, 384, 0, 0); XSync(dpy, False); XFreePixmap(dpy, pm); });
        XSetForeground(dpy, gc, WhitePixel(dpy, scr));
        TIME("x 2000 FillRect+Line+Str+Sync", 1, {
            for (i = 0; i < 2000; i++) {
                XFillRectangle(dpy, w, gc, (i * 7) % 400, (i * 13) % 300, 100, 60);
                XDrawLine(dpy, w, gc, 0, i % 384, 511, (i * 3) % 384);
                XDrawString(dpy, w, gc, 10, 20 + i % 300, "Window Maker benchmark", 22);
                if (i % 50 == 49) XSync(dpy, False);
            }
            XSync(dpy, False); });
        TIME("x XGetImage 256x256", 100, { xi = XGetImage(dpy, w, 0, 0, 256, 256, AllPlanes, ZPixmap); if (!xi) { puts("FAIL: getimage"); return 1; } XDestroyImage(xi); });
        XCloseDisplay(dpy);
    }
    puts("BENCHDONE");
    return 0;
}
