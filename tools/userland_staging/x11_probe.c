/* x11_probe (DAR-451): a small X client for checking a GUI from a serial
 * shell when there is no xdotool/xwininfo on the image.
 *   x11_probe tree            windows under the root, 4 levels: id, geometry,
 *                             (root-relative), map state, depth, visual, WM_NAME
 *   x11_probe click X Y [B]   XTest: move the pointer to X,Y and click
 *                             button B (default 1)
 *   x11_probe dclick X Y      XTest: double-click button 1 at X,Y
 *   x11_probe raise|lower|remap ID
 *                             XRaiseWindow / XLowerWindow / unmap+map on
 *                             window ID (hex or decimal)
 *   x11_probe move ID X Y     XMoveWindow
 *   x11_probe fill ID         core XFillRectangle (red) on the left half and
 *                             XRenderFillRectangle (green) on the right half
 * Uses $DISPLAY. Built by build_x11_probe.sh; not installed by
 * inject_into_sd_image.sh (copy it onto a test image by hand). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xrender.h>

static void tree(Display *d, Window w, int depth)
{
    Window root, parent, *kids = NULL, child;
    unsigned int n = 0, i;
    XWindowAttributes a;
    char *name = NULL;
    int rx = 0, ry = 0;

    if (!XGetWindowAttributes(d, w, &a))
        return;
    XTranslateCoordinates(d, w, a.root, 0, 0, &rx, &ry, &child);
    XFetchName(d, w, &name);
    printf("%*s0x%lx %dx%d+%d+%d %s depth=%d visual=0x%lx%s%s%s\n", depth * 2, "", (unsigned long)w,
           a.width, a.height, rx, ry,
           a.map_state == IsViewable ? "viewable" : a.map_state == IsUnmapped ? "unmapped" : "unviewable",
           a.depth, (unsigned long)XVisualIDFromVisual(a.visual),
           a.override_redirect ? " override" : "", name ? " name=" : "", name ? name : "");
    if (name)
        XFree(name);
    if (depth >= 4)
        return;
    if (!XQueryTree(d, w, &root, &parent, &kids, &n))
        return;
    for (i = 0; i < n; i++)
        tree(d, kids[i], depth + 1);
    if (kids)
        XFree(kids);
}

static void click(Display *d, int x, int y, unsigned int b, int times)
{
    int i;

    XTestFakeMotionEvent(d, -1, x, y, CurrentTime);
    XSync(d, False);
    usleep(200000);
    for (i = 0; i < times; i++) {
        XTestFakeButtonEvent(d, b, True, CurrentTime);
        XTestFakeButtonEvent(d, b, False, CurrentTime);
        XSync(d, False);
        usleep(100000);
    }
}

int main(int argc, char **argv)
{
    Display *d = XOpenDisplay(NULL);
    int ev, er, maj, min;

    if (!d) {
        fprintf(stderr, "x11_probe: cannot open display\n");
        return 1;
    }
    if (argc >= 2 && !strcmp(argv[1], "tree")) {
        tree(d, DefaultRootWindow(d), 0);
    } else if (argc >= 4 && (!strcmp(argv[1], "click") || !strcmp(argv[1], "dclick"))) {
        if (!XTestQueryExtension(d, &ev, &er, &maj, &min)) {
            fprintf(stderr, "x11_probe: no XTEST extension\n");
            return 1;
        }
        click(d, atoi(argv[2]), atoi(argv[3]), argc >= 5 ? (unsigned)atoi(argv[4]) : 1,
              !strcmp(argv[1], "dclick") ? 2 : 1);
        printf("x11_probe: %s %s,%s done\n", argv[1], argv[2], argv[3]);
    } else if (argc >= 3 && (!strcmp(argv[1], "raise") || !strcmp(argv[1], "lower") || !strcmp(argv[1], "remap"))) {
        Window w = (Window)strtoul(argv[2], NULL, 0);

        if (!strcmp(argv[1], "raise"))
            XRaiseWindow(d, w);
        else if (!strcmp(argv[1], "lower"))
            XLowerWindow(d, w);
        else {
            XUnmapWindow(d, w);
            XSync(d, False);
            XMapWindow(d, w);
        }
        XSync(d, False);
        printf("x11_probe: %s 0x%lx done\n", argv[1], (unsigned long)w);
    } else if (argc >= 5 && !strcmp(argv[1], "move")) {
        Window w = (Window)strtoul(argv[2], NULL, 0);

        XMoveWindow(d, w, atoi(argv[3]), atoi(argv[4]));
        XSync(d, False);
        printf("x11_probe: move 0x%lx done\n", (unsigned long)w);
    } else if (argc >= 3 && !strcmp(argv[1], "fill")) {
        Window w = (Window)strtoul(argv[2], NULL, 0);
        XWindowAttributes a;
        XRenderPictFormat *fmt;
        XRenderColor green = { 0, 0xffff, 0, 0xffff };
        GC gc;

        XGetWindowAttributes(d, w, &a);
        gc = XCreateGC(d, w, 0, NULL);
        XSetForeground(d, gc, 0xffff0000UL);
        XFillRectangle(d, w, gc, 0, 0, a.width / 2, a.height);
        fmt = XRenderFindVisualFormat(d, a.visual);
        if (fmt) {
            Picture p = XRenderCreatePicture(d, w, fmt, 0, NULL);
            XRenderFillRectangle(d, PictOpSrc, p, &green, a.width / 2, 0, a.width - a.width / 2, a.height);
            XRenderFreePicture(d, p);
        }
        XSync(d, False);
        printf("x11_probe: fill 0x%lx %dx%d done (render format %s)\n", (unsigned long)w, a.width, a.height, fmt ? "yes" : "no");
    } else {
        fprintf(stderr, "usage: x11_probe tree | click X Y [button] | dclick X Y | raise|lower|remap ID | move ID X Y | fill ID\n");
        return 2;
    }
    XCloseDisplay(d);
    return 0;
}
