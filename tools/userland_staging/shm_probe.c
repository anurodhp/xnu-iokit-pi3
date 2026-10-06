/*
 * shm_probe: checks each step MIT-SHM needs on this system (SysV shmget/
 * shmat, the server's MIT-SHM extension, XShmAttach) and prints which one
 * fails. Used to bring up the GL present path's MIT-SHM use
 * (mesa_xlib_vc4/xlib_vc4.c). Usage: shm_probe [display] [bytes]
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>

static int xerr;
static int
handler(Display *d, XErrorEvent *e)
{
	char buf[128];
	XGetErrorText(d, e->error_code, buf, sizeof(buf));
	printf("X error: %s (request %d.%d)\n", buf, e->request_code, e->minor_code);
	xerr = 1;
	return 0;
}

int
main(int argc, char **argv)
{
	size_t bytes = argc > 2 ? strtoul(argv[2], NULL, 0) : 1000 * 700 * 4;
	int id = shmget(IPC_PRIVATE, bytes, IPC_CREAT | 0600);
	printf("shmget(%zu): %d %s\n", bytes, id, id < 0 ? strerror(errno) : "ok");
	if (id < 0)
		return 1;
	char *p = shmat(id, NULL, 0);
	printf("shmat: %s\n", p == (char *)-1 ? strerror(errno) : "ok");
	if (p == (char *)-1)
		return 1;
	memset(p, 0x55, bytes);
	Display *d = XOpenDisplay(argc > 1 ? argv[1] : NULL);
	if (d == NULL) {
		printf("no display\n");
		return 1;
	}
	int major, minor;
	Bool pix;
	printf("XShmQueryVersion: %s\n", XShmQueryVersion(d, &major, &minor, &pix) ? "yes" : "NO");
	XShmSegmentInfo info = { .shmid = id, .shmaddr = p, .readOnly = True };
	XSetErrorHandler(handler);
	XShmAttach(d, &info);
	XSync(d, False);
	printf("XShmAttach: %s\n", xerr ? "FAILED" : "ok");
	shmctl(id, IPC_RMID, NULL);
	return xerr;
}
