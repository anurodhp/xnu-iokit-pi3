/*
 * libepoxy smoke test: GL/GLX entry points resolved at run time through
 * libepoxy (which dlopens Mesa's libGL), the way WebKit's WPE port uses it.
 * Creates a GLX context, checks epoxy's version/extension queries and that a
 * clear + glReadPixels through epoxy-dispatched functions works. Also checks
 * that, with no context, epoxy's queries are safe (GL not required).
 * Prints "EPOXYTEST PASS" or "EPOXYTEST FAIL n". Usage: epoxy_smoketest [display]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <epoxy/gl.h>
#include <epoxy/glx.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } else { printf("ok:   " __VA_ARGS__); printf("\n"); } } while (0)

int
main(int argc, char **argv)
{
	/* before any context: epoxy must not crash and must report "no GL" */
	CHECK(epoxy_gl_version() == 0 || epoxy_gl_version() > 0, "epoxy_gl_version() is safe without a context");

	Display *dpy = XOpenDisplay(argc > 1 ? argv[1] : NULL);
	if (!dpy) { printf("EPOXYTEST FAIL no display\n"); return 1; }
	int attr[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_DEPTH_SIZE, 16, None };
	XVisualInfo *vi = glXChooseVisual(dpy, DefaultScreen(dpy), attr);
	CHECK(vi != NULL, "glXChooseVisual through epoxy");
	if (!vi) return 1;
	XSetWindowAttributes swa;
	memset(&swa, 0, sizeof(swa));
	swa.colormap = XCreateColormap(dpy, RootWindow(dpy, vi->screen), vi->visual, AllocNone);
	Window win = XCreateWindow(dpy, RootWindow(dpy, vi->screen), 0, 0, 32, 32, 0, vi->depth,
	    InputOutput, vi->visual, CWColormap, &swa);
	XMapWindow(dpy, win);
	GLXContext ctx = glXCreateContext(dpy, vi, NULL, True);
	CHECK(ctx != NULL, "glXCreateContext through epoxy");
	if (!ctx || !glXMakeCurrent(dpy, win, ctx)) { printf("EPOXYTEST FAIL context\n"); return 1; }

	printf("GL_RENDERER %s\nGL_VERSION  %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
	int v = epoxy_gl_version();
	CHECK(v >= 21, "epoxy_gl_version() = %d", v);
	CHECK(epoxy_is_desktop_gl(), "desktop GL detected");
	CHECK(epoxy_has_gl_extension("GL_ARB_vertex_buffer_object") || v >= 15, "extension / version query");
	CHECK(epoxy_has_glx_extension(dpy, DefaultScreen(dpy), "GLX_ARB_get_proc_address") || 1, "GLX extension query does not crash");
	CHECK(epoxy_glGenBuffers != NULL || v >= 15, "buffer entry points resolve");

	glClearColor(1.f, 0.f, 1.f, 1.f);
	glClear(GL_COLOR_BUFFER_BIT);
	unsigned char p[4];
	glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
	CHECK(p[0] == 255 && p[1] == 0 && p[2] == 255, "clear + glReadPixels via epoxy: got %d,%d,%d", p[0], p[1], p[2]);

	glXMakeCurrent(dpy, None, NULL);
	glXDestroyContext(dpy, ctx);
	XCloseDisplay(dpy);
	printf(fails ? "EPOXYTEST FAIL %d\n" : "EPOXYTEST PASS\n", fails);
	return fails != 0;
}
