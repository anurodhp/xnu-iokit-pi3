/*
 * OpenGL ES 2.0 smoke test: an ES2-profile context created through GLX
 * (GLX_EXT_create_context_es2_profile) on Mesa's libGL, with the GLES2 API
 * from libGLESv2. Draws a flat triangle with GLSL ES 1.00 shaders and checks
 * pixels with glReadPixels. Prints "GLES2TEST PASS" or "GLES2TEST FAIL n".
 * Usage: gles2_smoketest [display]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/glx.h>
#include <GLES2/gl2.h>

#ifndef GLX_CONTEXT_ES2_PROFILE_BIT_EXT
#define GLX_CONTEXT_ES2_PROFILE_BIT_EXT 0x00000004
#endif
#ifndef GLX_CONTEXT_PROFILE_MASK_ARB
#define GLX_CONTEXT_PROFILE_MASK_ARB 0x9126
#define GLX_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define GLX_CONTEXT_MINOR_VERSION_ARB 0x2092
#endif

typedef GLXContext (*create_ctx_fn)(Display *, GLXFBConfig, GLXContext, Bool, const int *);

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static GLuint
compile(GLenum type, const char *src)
{
	GLuint s = glCreateShader(type);
	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	GLint ok = 0;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[512];
		glGetShaderInfoLog(s, sizeof(log), NULL, log);
		printf("shader log: %s\n", log);
	}
	CHECK(ok, "%s shader compiles", type == GL_VERTEX_SHADER ? "vertex" : "fragment");
	return s;
}

int
main(int argc, char **argv)
{
	Display *dpy = XOpenDisplay(argc > 1 ? argv[1] : NULL);
	if (!dpy) { printf("GLES2TEST FAIL no display\n"); return 1; }
	int fbattr[] = { GLX_RENDER_TYPE, GLX_RGBA_BIT, GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
	    GLX_DOUBLEBUFFER, True, GLX_DEPTH_SIZE, 16, None };
	int n = 0;
	GLXFBConfig *fbc = glXChooseFBConfig(dpy, DefaultScreen(dpy), fbattr, &n);
	if (!fbc || n == 0) { printf("GLES2TEST FAIL no fbconfig\n"); return 1; }
	XVisualInfo *vi = glXGetVisualFromFBConfig(dpy, fbc[0]);
	XSetWindowAttributes swa;
	memset(&swa, 0, sizeof(swa));
	swa.colormap = XCreateColormap(dpy, RootWindow(dpy, vi->screen), vi->visual, AllocNone);
	Window win = XCreateWindow(dpy, RootWindow(dpy, vi->screen), 0, 0, 64, 64, 0, vi->depth,
	    InputOutput, vi->visual, CWColormap, &swa);
	XMapWindow(dpy, win);

	create_ctx_fn create = (create_ctx_fn)glXGetProcAddress((const GLubyte *)"glXCreateContextAttribsARB");
	if (!create) { printf("GLES2TEST FAIL no glXCreateContextAttribsARB\n"); return 1; }
	int ctxattr[] = { GLX_CONTEXT_MAJOR_VERSION_ARB, 2, GLX_CONTEXT_MINOR_VERSION_ARB, 0,
	    GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_ES2_PROFILE_BIT_EXT, None };
	GLXContext ctx = create(dpy, fbc[0], NULL, True, ctxattr);
	if (!ctx) { printf("GLES2TEST FAIL ES2 context\n"); return 1; }
	if (!glXMakeCurrent(dpy, win, ctx)) { printf("GLES2TEST FAIL make current\n"); return 1; }
	printf("GL_RENDERER %s\nGL_VERSION  %s\nGLSL        %s\n", glGetString(GL_RENDERER),
	    glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));
	CHECK(strstr((const char *)glGetString(GL_VERSION), "OpenGL ES 2.") != NULL || strstr((const char *)glGetString(GL_VERSION), "OpenGL ES 3.") != NULL, "an OpenGL ES 2+ context (Mesa gives the highest ES it supports)");

	const char *vs = "attribute vec4 pos; void main() { gl_Position = pos; }";
	const char *fs = "precision mediump float; uniform vec4 color; void main() { gl_FragColor = color; }";
	GLuint prog = glCreateProgram();
	glAttachShader(prog, compile(GL_VERTEX_SHADER, vs));
	glAttachShader(prog, compile(GL_FRAGMENT_SHADER, fs));
	glBindAttribLocation(prog, 0, "pos");
	glLinkProgram(prog);
	GLint ok = 0;
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	CHECK(ok, "program links");
	glUseProgram(prog);
	glUniform4f(glGetUniformLocation(prog, "color"), 0.f, 1.f, 0.f, 1.f);

	static const GLfloat tri[] = { -1.f, -1.f, 0.f, 1.f,   1.f, -1.f, 0.f, 1.f,   -1.f, 1.f, 0.f, 1.f };
	glViewport(0, 0, 64, 64);
	glClearColor(0.f, 0.f, 1.f, 1.f);
	glClear(GL_COLOR_BUFFER_BIT);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, tri);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	unsigned char p[4];
	glReadPixels(10, 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
	CHECK(p[0] == 0 && p[1] == 255 && p[2] == 0, "inside the triangle is green: got %d,%d,%d", p[0], p[1], p[2]);
	glReadPixels(60, 60, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
	CHECK(p[0] == 0 && p[1] == 0 && p[2] == 255, "outside the triangle is the clear colour: got %d,%d,%d", p[0], p[1], p[2]);
	CHECK(glGetError() == GL_NO_ERROR, "no GL error");

	glXSwapBuffers(dpy, win);
	XSync(dpy, False);
	glXMakeCurrent(dpy, None, NULL);
	glXDestroyContext(dpy, ctx);
	XCloseDisplay(dpy);
	if (fails) { printf("GLES2TEST FAIL %d\n", fails); return 1; }
	printf("GLES2TEST PASS\n");
	return 0;
}
