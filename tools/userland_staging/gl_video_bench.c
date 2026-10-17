/*
 * gl_video_bench: what a video player's GL frame costs on this libGL, step
 * by step. Each frame does what mpv's OpenGL renderer does for a YUV 4:2:0
 * picture drawn 1:1 (Venice's gl renderer): upload the three planes with
 * glTexSubImage2D (GL_LUMINANCE, the GL 2.1 path), draw one quad with a
 * YUV->RGB fragment shader, swap.
 *
 *   gl_video_bench [width height [frames]]      (default 1280 720 200)
 *
 * Prints per frame: upload (the three glTexSubImage2D calls), draw (the
 * draw call, CPU side), gpu (glFinish: the GPU running the frame), swap
 * (glXSwapBuffers: on vc4, xlib_vc4's read-back and put; XLIB_VC4_STATS=1
 * splits it), and the total. Environment: DISPLAY, and on the Pi the
 * usual XAUTHORITY.
 *
 * Built by build_gl_video_bench.sh.
 */
#define GL_GLEXT_PROTOTYPES 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>

static double
now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static const char *vs =
    "attribute vec2 pos;\n"
    "varying vec2 tc;\n"
    "void main() { tc = pos * 0.5 + 0.5; gl_Position = vec4(pos, 0.0, 1.0); }\n";
static const char *fs =
    "uniform sampler2D ty, tu, tv;\n"
    "varying vec2 tc;\n"
    "void main() {\n"
    "  float y = texture2D(ty, tc).r - 0.0625, u = texture2D(tu, tc).r - 0.5, v = texture2D(tv, tc).r - 0.5;\n"
    "  gl_FragColor = vec4(1.164 * y + 1.596 * v, 1.164 * y - 0.392 * u - 0.813 * v, 1.164 * y + 2.017 * u, 1.0);\n"
    "}\n";

static GLuint
shader(GLenum type, const char *src)
{
	GLuint s = glCreateShader(type);
	GLint ok = 0;
	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[512];
		glGetShaderInfoLog(s, sizeof(log), NULL, log);
		fprintf(stderr, "shader: %s\n", log);
		exit(1);
	}
	return s;
}

int
main(int argc, char **argv)
{
	int w = argc > 2 ? atoi(argv[1]) : 1280, h = argc > 2 ? atoi(argv[2]) : 720;
	int frames = argc > 3 ? atoi(argv[3]) : 200;
	Display *dpy = XOpenDisplay(NULL);
	if (dpy == NULL) {
		fprintf(stderr, "no display\n");
		return 1;
	}
	int attr[] = { GLX_RGBA, GLX_DOUBLEBUFFER, None };
	XVisualInfo *vi = glXChooseVisual(dpy, DefaultScreen(dpy), attr);
	Window root = RootWindow(dpy, vi->screen);
	XSetWindowAttributes swa;
	memset(&swa, 0, sizeof(swa));
	swa.colormap = XCreateColormap(dpy, root, vi->visual, AllocNone);
	Window win = XCreateWindow(dpy, root, 0, 0, (unsigned)w, (unsigned)h, 0, vi->depth, InputOutput,
	    vi->visual, CWColormap, &swa);
	XMapWindow(dpy, win);
	XSync(dpy, False);
	GLXContext ctx = glXCreateContext(dpy, vi, NULL, True);
	glXMakeCurrent(dpy, win, ctx);
	printf("GL_RENDERER %s, %dx%d, %d frames\n", glGetString(GL_RENDERER), w, h, frames);

	GLuint prog = glCreateProgram();
	glAttachShader(prog, shader(GL_VERTEX_SHADER, vs));
	glAttachShader(prog, shader(GL_FRAGMENT_SHADER, fs));
	glBindAttribLocation(prog, 0, "pos");
	glLinkProgram(prog);
	glUseProgram(prog);
	const char *names[3] = { "ty", "tu", "tv" };
	int pw[3] = { w, w / 2, w / 2 }, ph[3] = { h, h / 2, h / 2 };
	GLuint tex[3];
	unsigned char *plane[3];
	glGenTextures(3, tex);
	for (int i = 0; i < 3; i++) {
		plane[i] = malloc((size_t)pw[i] * ph[i]);
		for (int k = 0; k < pw[i] * ph[i]; k++) {
			plane[i][k] = (unsigned char)(k * 7 + i * 50);
		}
		glActiveTexture(GL_TEXTURE0 + i);
		glBindTexture(GL_TEXTURE_2D, tex[i]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, pw[i], ph[i], 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, plane[i]);
		glUniform1i(glGetUniformLocation(prog, names[i]), i);
	}
	static const float quad[8] = { -1, -1, 1, -1, -1, 1, 1, 1 };
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
	glEnableVertexAttribArray(0);
	glViewport(0, 0, w, h);

	double t_up = 0, t_draw = 0, t_gpu = 0, t_swap = 0;
	double start = now_ms();
	for (int f = 0; f < frames; f++) {
		double t0 = now_ms();
		for (int i = 0; i < 3; i++) {
			plane[i][f % (pw[i] * ph[i])] ^= 0xff;  /* a changed picture every frame */
			glActiveTexture(GL_TEXTURE0 + i);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, pw[i], ph[i], GL_LUMINANCE, GL_UNSIGNED_BYTE, plane[i]);
		}
		double t1 = now_ms();
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		double t2 = now_ms();
		glFinish();
		double t3 = now_ms();
		glXSwapBuffers(dpy, win);
		double t4 = now_ms();
		if (f >= 10) {          /* skip the warm-up */
			t_up += t1 - t0;
			t_draw += t2 - t1;
			t_gpu += t3 - t2;
			t_swap += t4 - t3;
		}
	}
	int n = frames - 10;
	double total = now_ms() - start;
	printf("per frame: upload %.2f ms, draw %.2f, gpu %.2f, swap %.2f; total %.2f (%.1f fps)\n",
	    t_up / n, t_draw / n, t_gpu / n, t_swap / n, (t_up + t_draw + t_gpu + t_swap) / n, frames * 1000.0 / total);
	return 0;
}
