/*
 * wmlat (DAR-431): Window Maker event-loop latency probe.
 *   wmlat rt N      N serial round trips: XMoveWindow on a managed window,
 *                   time until Window Maker's (synthetic) ConfigureNotify
 *                   reaches this client. Prints min/median/mean/p95/max in
 *                   microseconds. Window Maker handles the ConfigureRequest
 *                   in its EventLoop (third_party/wmaker/src/event.c
 *                   handleConfigureRequest -> wClientConfigure), so this is
 *                   X server -> wmaker wakeup -> handler -> reply.
 *   wmlat burst N   N XTest pointer-motion events queued at once followed
 *                   by one XMoveWindow; total time until that move is
 *                   acknowledged, i.e. until wmaker has drained the burst.
 * Uses $DISPLAY. A QEMU test aid, installed as /usr/X11/bin/wmlat.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/select.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>

static long long now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000LL + tv.tv_usec;
}

static int cmp(const void *a, const void *b)
{
    long long x = *(const long long *)a, y = *(const long long *)b;
    return x < y ? -1 : x > y;
}

/* Wait up to ms for a synthetic ConfigureNotify on w; 1 if seen. Blocks in
 * select() on the X fd (not a usleep poll, whose granularity would swamp
 * what is being measured). */
static int wait_cn(Display *d, Window w, int ms)
{
    long long end = now_us() + ms * 1000LL;
    XEvent ev;
    int fd = ConnectionNumber(d);
    for (;;) {
        long long left;
        struct timeval tv;
        fd_set rs;
        while (XPending(d)) {
            XNextEvent(d, &ev);
            if (ev.type == ConfigureNotify && ev.xconfigure.window == w && ev.xconfigure.send_event)
                return 1;
        }
        left = end - now_us();
        if (left <= 0)
            return 0;
        tv.tv_sec = left / 1000000;
        tv.tv_usec = left % 1000000;
        FD_ZERO(&rs);
        FD_SET(fd, &rs);
        select(fd + 1, &rs, NULL, NULL, &tv);
    }
}

int main(int argc, char **argv)
{
    Display *d = XOpenDisplay(NULL);
    Window w;
    XEvent ev;
    int n, i, lost = 0, scr;
    long long *v, t0, t1, sum = 0;
    XSizeHints sh;

    if (argc < 3 || !d) {
        fprintf(stderr, "usage: wmlat rt|burst N  (needs $DISPLAY)\n");
        return 2;
    }
    n = atoi(argv[2]);
    scr = DefaultScreen(d);
    w = XCreateSimpleWindow(d, RootWindow(d, scr), 100, 100, 200, 100, 1, BlackPixel(d, scr), WhitePixel(d, scr));
    memset(&sh, 0, sizeof sh);
    sh.flags = USPosition | USSize;
    XSetWMNormalHints(d, w, &sh);
    XStoreName(d, w, "wmlat");
    XSelectInput(d, w, StructureNotifyMask);
    XMapWindow(d, w);
    XSync(d, False);
    for (i = 0; i < 100; i++) {   /* wait until mapped, then let wmaker settle */
        if (XCheckTypedWindowEvent(d, w, MapNotify, &ev))
            break;
        usleep(50000);
    }
    sleep(2);
    while (XPending(d))
        XNextEvent(d, &ev);

    if (!strcmp(argv[1], "rt")) {
        v = calloc(n, sizeof *v);
        for (i = 0; i < n; i++) {
            t0 = now_us();
            XMoveWindow(d, w, 100 + (i & 1) * 40, 100 + (i & 2) * 10);
            XFlush(d);
            if (!wait_cn(d, w, 2000)) { lost++; v[i] = 2000000; continue; }
            t1 = now_us();
            v[i] = t1 - t0;
        }
        qsort(v, n, sizeof *v, cmp);
        for (i = 0; i < n; i++)
            sum += v[i];
        printf("rt n=%d lost=%d min=%lld median=%lld mean=%lld p95=%lld max=%lld us\n", n, lost, v[0],
               v[n / 2], sum / n, v[(n * 95) / 100], v[n - 1]);
    } else {
        int w0 = DisplayWidth(d, scr), h0 = DisplayHeight(d, scr);
        t0 = now_us();
        for (i = 0; i < n; i++)
            XTestFakeMotionEvent(d, scr, 10 + (i * 7) % (w0 - 20), 10 + (i * 13) % (h0 - 20), 0);
        XMoveWindow(d, w, 140, 100);
        XFlush(d);
        if (!wait_cn(d, w, 240000)) { printf("burst: timeout\n"); return 1; }
        t1 = now_us();
        printf("burst n=%d total=%lld us (%.1f us/event)\n", n, t1 - t0, (double)(t1 - t0) / n);
    }
    return 0;
}
