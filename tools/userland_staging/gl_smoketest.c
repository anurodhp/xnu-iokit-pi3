/*
 * Software OpenGL smoke test (docs/gpu-opengl-plan.md, Tier A). Links
 * /usr/X11/lib/libGL.1.dylib (Mesa 25.2.8, GLX over Xlib, softpipe) and
 * checks, against a running X server (Xvfb):
 *   1. glXChooseVisual / glXCreateContext / glXMakeCurrent succeed
 *   2. GL_VENDOR / GL_RENDERER / GL_VERSION are reported
 *   3. fixed function: clear + a flat triangle, verified with glReadPixels
 *   4. GLSL: a vertex+fragment program compiles, links and draws (this
 *      runs Mesa's C++ GLSL front end and NIR in libc++)
 *   5. glXSwapBuffers does not fault
 * Prints "GLTEST PASS" or "GLTEST FAIL <n>". Usage: gl_smoketest [display]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/gl.h>
#include <GL/glx.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static int px(int x, int y, unsigned char out[4])
{
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, out);
    return glGetError() == GL_NO_ERROR;
}

int main(int argc, char **argv)
{
    Display *dpy = XOpenDisplay(argc > 1 ? argv[1] : NULL);
    if (!dpy) { printf("GLTEST FAIL no display\n"); return 1; }
    int attr[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_DEPTH_SIZE, 16, None };
    XVisualInfo *vi = glXChooseVisual(dpy, DefaultScreen(dpy), attr);
    if (!vi) { printf("GLTEST FAIL glXChooseVisual\n"); return 1; }
    Window root = RootWindow(dpy, vi->screen);
    XSetWindowAttributes swa;
    memset(&swa, 0, sizeof swa);
    swa.colormap = XCreateColormap(dpy, root, vi->visual, AllocNone);
    swa.event_mask = ExposureMask;
    Window win = XCreateWindow(dpy, root, 0, 0, 128, 128, 0, vi->depth, InputOutput,
                               vi->visual, CWColormap | CWEventMask, &swa);
    XMapWindow(dpy, win);
    GLXContext ctx = glXCreateContext(dpy, vi, NULL, True);
    if (!ctx) { printf("GLTEST FAIL glXCreateContext\n"); return 1; }
    if (!glXMakeCurrent(dpy, win, ctx)) { printf("GLTEST FAIL glXMakeCurrent\n"); return 1; }
    printf("GL_VENDOR   %s\nGL_RENDERER %s\nGL_VERSION  %s\n",
           glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
    CHECK(glGetString(GL_RENDERER) != NULL, "no GL_RENDERER");

    /* 3. fixed function */
    unsigned char p[4];
    glViewport(0, 0, 128, 128);
    glClearColor(0.f, 0.f, 1.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    CHECK(px(5, 5, p) && p[0] == 0 && p[1] == 0 && p[2] == 255, "clear: got %d,%d,%d", p[0], p[1], p[2]);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glColor3f(1.f, 0.f, 0.f);
    glBegin(GL_TRIANGLES);
    glVertex2f(-1.f, -1.f); glVertex2f(1.f, -1.f); glVertex2f(-1.f, 1.f);
    glEnd();
    CHECK(px(20, 20, p) && p[0] == 255 && p[1] == 0 && p[2] == 0, "triangle inside: got %d,%d,%d", p[0], p[1], p[2]);
    CHECK(px(120, 120, p) && p[0] == 0 && p[2] == 255, "triangle outside: got %d,%d,%d", p[0], p[1], p[2]);

    /* 4. GLSL (OpenGL 2.0 entry points, fetched through glXGetProcAddress) */
    typedef GLuint (*f_cs)(GLenum); typedef void (*f_ss)(GLuint, GLsizei, const char *const *, const GLint *);
    typedef void (*f_c)(GLuint); typedef void (*f_iv)(GLuint, GLenum, GLint *);
    typedef GLuint (*f_cp)(void); typedef void (*f_ap)(GLuint, GLuint); typedef void (*f_up)(GLuint);
    f_cs createShader = (f_cs)glXGetProcAddress((const GLubyte *)"glCreateShader");
    f_ss shaderSource = (f_ss)glXGetProcAddress((const GLubyte *)"glShaderSource");
    f_c compileShader = (f_c)glXGetProcAddress((const GLubyte *)"glCompileShader");
    f_iv getShaderiv = (f_iv)glXGetProcAddress((const GLubyte *)"glGetShaderiv");
    f_cp createProgram = (f_cp)glXGetProcAddress((const GLubyte *)"glCreateProgram");
    f_ap attachShader = (f_ap)glXGetProcAddress((const GLubyte *)"glAttachShader");
    f_c linkProgram = (f_c)glXGetProcAddress((const GLubyte *)"glLinkProgram");
    f_iv getProgramiv = (f_iv)glXGetProcAddress((const GLubyte *)"glGetProgramiv");
    f_up useProgram = (f_up)glXGetProcAddress((const GLubyte *)"glUseProgram");
    if (createShader && shaderSource && compileShader && getShaderiv && createProgram &&
        attachShader && linkProgram && getProgramiv && useProgram) {
        static const char *vs = "void main(){ gl_Position = gl_Vertex; }";
        static const char *fs = "void main(){ gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0); }";
        GLuint v = createShader(GL_VERTEX_SHADER), f = createShader(GL_FRAGMENT_SHADER);
        GLint ok = 0;
        shaderSource(v, 1, &vs, NULL); compileShader(v); getShaderiv(v, GL_COMPILE_STATUS, &ok);
        CHECK(ok, "vertex shader compile");
        shaderSource(f, 1, &fs, NULL); compileShader(f); getShaderiv(f, GL_COMPILE_STATUS, &ok);
        CHECK(ok, "fragment shader compile");
        GLuint prog = createProgram();
        attachShader(prog, v); attachShader(prog, f); linkProgram(prog);
        getProgramiv(prog, GL_LINK_STATUS, &ok);
        CHECK(ok, "program link");
        if (ok) {
            useProgram(prog);
            glClear(GL_COLOR_BUFFER_BIT);
            glBegin(GL_TRIANGLES);
            glVertex2f(-1.f, -1.f); glVertex2f(1.f, -1.f); glVertex2f(-1.f, 1.f);
            glEnd();
            CHECK(px(20, 20, p) && p[0] == 0 && p[1] == 255 && p[2] == 0, "GLSL triangle: got %d,%d,%d", p[0], p[1], p[2]);
        }
    } else {
        fails++; printf("FAIL: GL 2.0 shader entry points missing\n");
    }

    /* 5. present */
    glXSwapBuffers(dpy, win);
    XSync(dpy, False);
    glXMakeCurrent(dpy, None, NULL);
    glXDestroyContext(dpy, ctx);
    XCloseDisplay(dpy);
    if (fails) { printf("GLTEST FAIL %d\n", fails); return 1; }
    printf("GLTEST PASS\n");
    return 0;
}
