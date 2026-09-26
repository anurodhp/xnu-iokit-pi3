/*
 * X11 client-library smoketest (DAR-383, X11GS 2).
 *
 * Two parts:
 *
 * 1. Compile-time: the arm64 sizing the ticket asks about. Client-side XID
 *    is `unsigned long` (xorgproto X.h, non-_XSERVER64 branch) -- 8 bytes
 *    on LP64, exactly as on Linux/aarch64 -- while every wire type is
 *    32-bit: Xmd.h picks LONG64 from __SIZEOF_LONG__ == 8, CARD32 stays
 *    4 bytes, and xcb's generated xcb_window_t is uint32_t. The static
 *    asserts below fail the build if any of that changes.
 *
 * 2. Run-time, with or without an X server:
 *    - keysym tables (libX11's generated ks_tables.h) round-trip;
 *    - XOpenDisplay() on a display with no server must return NULL
 *      cleanly (the AF_UNIX connect to /tmp/.X11-unix/X<n> fails), and
 *      xcb_connect() must report a connection error rather than crash;
 *    - if argv[1] names a display that DOES answer (X11GS 4's Xvfb), the
 *      ticket's full path runs: XOpenDisplay, XCreateSimpleWindow,
 *      XMapWindow, XFlush, plus one query per extension library; and
 *      (DAR-387) an XFillRectangle read back through XGetImage and again
 *      through an MIT-SHM XShmGetImage, which exercises the kernel's SysV
 *      shm (DAR-407) end to end.
 *
 * Every X dylib built by DAR-383 is referenced (the table below), so
 * loading this binary makes dyld resolve all of their binds for real.
 *
 * Usage: x11_smoketest [display]      (default ":0")
 * Exit status 0 = PASS.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <signal.h>
#include <poll.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xmd.h>
#include <X11/Xproto.h>
#include <X11/keysym.h>
#include <X11/Xlib-xcb.h>
#include <X11/Xauth.h>
#include <X11/Xdmcp.h>
#include <X11/extensions/Xext.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/ICE/ICElib.h>
#include <X11/SM/SMlib.h>
#include <X11/Intrinsic.h>
#include <X11/Xmu/Xmu.h>
#include <xcb/xcb.h>
#include <xcb/shm.h>
#include <xcb/render.h>

#ifndef LONG64
#error "Xmd.h did not define LONG64 on arm64"
#endif
_Static_assert(sizeof(long) == 8, "LP64 target expected");
_Static_assert(sizeof(XID) == sizeof(unsigned long), "client XID is unsigned long (non-_XSERVER64)");
_Static_assert(sizeof(CARD32) == 4, "CARD32 must be 32-bit on the wire");
_Static_assert(sizeof(xcb_window_t) == 4, "xcb_window_t is the 32-bit wire XID");
_Static_assert(sizeof(xCreateWindowReq) == sz_xCreateWindowReq, "xCreateWindowReq wire size");
_Static_assert(sizeof(xResourceReq) == sz_xResourceReq, "xResourceReq wire size");

/* One entry point per library, so every dylib is a real load-time
 * dependency whose binds dyld must resolve. */
static const struct { const char *lib; void *fn; } k_libs[] = {
    { "libX11",        (void *)XOpenDisplay },
    { "libX11-xcb",    (void *)XGetXCBConnection },
    { "libxcb",        (void *)xcb_connect },
    { "libxcb-shm",    (void *)xcb_shm_query_version },
    { "libxcb-render", (void *)xcb_render_query_version },
    { "libXau",        (void *)XauFileName },
    { "libXdmcp",      (void *)XdmcpReadHeader },
    { "libXext",       (void *)XShapeQueryExtension },
    { "libXrender",    (void *)XRenderQueryExtension },
    { "libXfixes",     (void *)XFixesQueryExtension },
    { "libXrandr",     (void *)XRRQueryExtension },
    { "libXcursor",    (void *)XcursorGetTheme },
    { "libICE",        (void *)IceOpenConnection },
    { "libSM",         (void *)SmcOpenConnection },
    { "libXt",         (void *)XtToolkitInitialize },
    { "libXmu",        (void *)XmuInternAtom },
};

static int failures;
static jmp_buf g_jb;
static volatile sig_atomic_t g_sigs;
static void on_usr1(int sig) { (void)sig; g_sigs++; longjmp(g_jb, 7); }
static void atexit_probe(void) { }
#define CHECK(cond, ...) do { if (cond) { printf("  ok   "); } else { printf("  FAIL "); failures++; } \
    printf(__VA_ARGS__); printf("\n"); } while (0)

static int x_error_code;
static int record_x_error(Display *d, XErrorEvent *e)
{
    (void)d;
    x_error_code = e->error_code;
    return 0;
}

static int run_with_server(Display *dpy)
{
    int scr = DefaultScreen(dpy);
    int ev, er, maj, min;
    Window w = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 10, 10, 200, 100, 1,
                                   BlackPixel(dpy, scr), WhitePixel(dpy, scr));
    CHECK(w != None, "XCreateSimpleWindow -> 0x%lx", (unsigned long)w);
    XStoreName(dpy, w, "x11_smoketest");
    XMapWindow(dpy, w);
    XFlush(dpy);
    XSync(dpy, False);
    printf("  info server vendor \"%s\" release %d, screen %dx%d depth %d\n",
           ServerVendor(dpy), VendorRelease(dpy),
           DisplayWidth(dpy, scr), DisplayHeight(dpy, scr), DefaultDepth(dpy, scr));
    printf("  info SHAPE %s, RENDER %s, XFIXES %s, RANDR %s\n",
           XShapeQueryExtension(dpy, &ev, &er) ? "yes" : "no",
           XRenderQueryExtension(dpy, &ev, &er) ? "yes" : "no",
           XFixesQueryExtension(dpy, &ev, &er) ? "yes" : "no",
           XRRQueryExtension(dpy, &ev, &er) ? "yes" : "no");
    if (XRRQueryVersion(dpy, &maj, &min))
        printf("  info RANDR %d.%d\n", maj, min);

    /* DAR-387: draw and read back. A red rectangle through the core
     * protocol, then XGetImage checks a pixel inside it and one of the
     * white background outside it. The red pixel value is built from the
     * default visual's masks (Xvfb's 24-bit TrueColor). */
    Visual *vis = DefaultVisual(dpy, scr);
    unsigned long red = vis->red_mask;
    unsigned long white = WhitePixel(dpy, scr);
    GC gc = XCreateGC(dpy, w, 0, NULL);
    XSetForeground(dpy, gc, red);
    XFillRectangle(dpy, w, gc, 20, 20, 50, 30);
    XSync(dpy, False);
    XImage *img = XGetImage(dpy, w, 0, 0, 100, 60, AllPlanes, ZPixmap);
    CHECK(img != NULL, "XGetImage of the window");
    if (img) {
        unsigned long in = XGetPixel(img, 40, 30), out = XGetPixel(img, 5, 5);
        CHECK((in & 0xffffff) == (red & 0xffffff),
              "XFillRectangle drew red (pixel 0x%06lx, want 0x%06lx)", in & 0xffffff, red & 0xffffff);
        CHECK((out & 0xffffff) == (white & 0xffffff),
              "background stayed white (pixel 0x%06lx)", out & 0xffffff);
        XDestroyImage(img);
    }

    /* MIT-SHM (DAR-407's SysV shm, end to end): read the same pixel back
     * through a shared-memory XImage the server writes into. */
    if (XShmQueryExtension(dpy)) {
        XShmSegmentInfo si;
        XImage *simg = XShmCreateImage(dpy, vis, DefaultDepth(dpy, scr), ZPixmap, NULL, &si, 100, 60);
        CHECK(simg != NULL, "XShmCreateImage");
        if (simg) {
            si.shmid = shmget(IPC_PRIVATE, (size_t)simg->bytes_per_line * simg->height, IPC_CREAT | 0600);
            si.shmaddr = simg->data = (si.shmid >= 0) ? shmat(si.shmid, NULL, 0) : (char *)-1;
            si.readOnly = False;
            CHECK(si.shmid >= 0 && si.shmaddr != (char *)-1, "shmget/shmat for the XShm image (id %d)", si.shmid);
            if (si.shmid >= 0 && si.shmaddr != (char *)-1) {
                /* IPC_RMID only after the server has attached: xnu refuses
                 * shmat() on a segment already marked removed
                 * (bsd/kern/sysv_shm.c:261, SHMSEG_REMOVED). A temporary
                 * error handler keeps a failed attach from exiting through
                 * Xlib's default handler before the segment is removed. */
                XErrorHandler prev = XSetErrorHandler(record_x_error);
                x_error_code = 0;
                Bool ok = XShmAttach(dpy, &si);
                XSync(dpy, False);
                XSetErrorHandler(prev);
                shmctl(si.shmid, IPC_RMID, NULL);
                ok = ok && x_error_code == 0;
                CHECK(ok, "XShmAttach (X error %d)", x_error_code);
                if (ok && XShmGetImage(dpy, w, simg, 0, 0, AllPlanes)) {
                    unsigned long px = XGetPixel(simg, 40, 30);
                    CHECK((px & 0xffffff) == (red & 0xffffff),
                          "XShmGetImage read the red pixel (0x%06lx)", px & 0xffffff);
                } else {
                    CHECK(0, "XShmGetImage");
                }
                if (ok) {
                    XShmDetach(dpy, &si);
                    XSync(dpy, False);
                }
                shmdt(si.shmaddr);
            }
            simg->data = NULL;
            XDestroyImage(simg);
        }
    } else {
        CHECK(0, "MIT-SHM extension present");
    }
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, w);
    XCloseDisplay(dpy);
    return 0;
}

int main(int argc, char **argv)
{
    const char *name = argc > 1 ? argv[1] : ":0";
    size_t i;

    printf("x11_smoketest (DAR-383)\n");
    {
        const char *d = getenv("DISPLAY");
        printf("  info DISPLAY=%s\n", d ? d : "(unset)");
    }
    for (i = 0; i < sizeof(k_libs) / sizeof(k_libs[0]); i++)
        CHECK(k_libs[i].fn != NULL, "%s entry point resolved (%p)", k_libs[i].lib, k_libs[i].fn);

    /* libSystem exports DAR-383 added for the X libraries (setjmp family
     * in libsystem_platform; signal/atexit in libsystem_c; poll/umask/
     * socket calls in libsystem_kernel): exercise each once for real. */
    {
        volatile int stage = 0;
        int r = setjmp(g_jb);
        if (r == 0) {
            stage = 1;
            longjmp(g_jb, 3);
        }
        CHECK(stage == 1 && r == 3, "setjmp/longjmp round trip (r=%d)", r);
    }
    {
        sigset_t cur;
        int r;
        CHECK(signal(SIGUSR1, on_usr1) != SIG_ERR, "signal(SIGUSR1) installs a handler");
        r = setjmp(g_jb);
        if (r == 0) {
            kill(getpid(), SIGUSR1);
        }
        sigprocmask(SIG_BLOCK, NULL, &cur);
        CHECK(r == 7 && g_sigs == 1 && !sigismember(&cur, SIGUSR1),
              "longjmp out of a signal handler restores the mask (r=%d sigs=%d)", r, (int)g_sigs);
        signal(SIGUSR1, SIG_DFL);
    }
    CHECK(atexit(atexit_probe) == 0, "atexit() registers");
    {
        mode_t old = umask(022);
        CHECK(umask(old) == 022, "umask round trip");
    }
    {
        int fds[2], v = 0, s;
        socklen_t vl = sizeof(v);
        struct pollfd pfd;
        CHECK(pipe(fds) == 0, "pipe()");
        write(fds[1], "x", 1);
        pfd.fd = fds[0]; pfd.events = POLLIN; pfd.revents = 0;
        CHECK(poll(&pfd, 1, 1000) == 1 && (pfd.revents & POLLIN), "poll() sees a readable pipe");
        close(fds[0]); close(fds[1]);
        s = socket(AF_UNIX, SOCK_STREAM, 0);
        CHECK(s >= 0, "socket(AF_UNIX)");
        CHECK(getsockopt(s, SOL_SOCKET, SO_TYPE, &v, &vl) == 0 && v == SOCK_STREAM, "getsockopt(SO_TYPE) == SOCK_STREAM");
        CHECK(shutdown(s, SHUT_RDWR) == -1 && errno == ENOTCONN, "shutdown() on an unconnected socket -> ENOTCONN");
        close(s);
    }

    CHECK(XStringToKeysym("Return") == XK_Return, "XStringToKeysym(\"Return\") == XK_Return");
    {
        const char *s = XKeysymToString(XK_space);
        CHECK(s && strcmp(s, "space") == 0, "XKeysymToString(XK_space) == \"space\" (%s)", s ? s : "(null)");
    }

    {
        int screen = -1;
        xcb_connection_t *c = xcb_connect(name, &screen);
        int err = xcb_connection_has_error(c);
        if (err) {
            CHECK(1, "xcb_connect(\"%s\") failed cleanly, error %d (no server)", name, err);
        } else {
            CHECK(1, "xcb_connect(\"%s\") connected (screen %d)", name, screen);
        }
        xcb_disconnect(c);
    }

    {
        Display *dpy = XOpenDisplay(name);
        if (dpy == NULL) {
            CHECK(1, "XOpenDisplay(\"%s\") returned NULL cleanly (no server)", name);
        } else {
            CHECK(1, "XOpenDisplay(\"%s\") connected", name);
            run_with_server(dpy);
        }
    }

    printf("x11_smoketest: %s (%d failure%s)\n", failures ? "FAIL" : "PASS",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
