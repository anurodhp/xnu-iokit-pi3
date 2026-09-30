/*
 * wutil_vnode_test (DAR-433): does EVFILT_VNODE deliver events through
 * libWUtil's kqueue (W_KQueueAddFilter) on a given filesystem, for the four
 * ways Window Maker's defaults files change? Usage:
 *   wutil_vnode_test [dir ...]        default: /tmp and $HOME
 * Each dir gets a scratch subdirectory (removed afterwards). A directory
 * watch (NOTE_WRITE|NOTE_DELETE|NOTE_RENAME) and a file watch (all vnode
 * notes, like wmaker's src/defaults.c) are registered the way wmaker does:
 * open(O_EVTONLY), EV_ADD|EV_CLEAR.
 *   A in-place write (append)            file: WRITE/EXTEND
 *   B atomic replace (tmp file + rename) file: DELETE (old vnode unlinked), dir: WRITE
 *   C unlink + recreate                  file: DELETE, dir: WRITE
 *   D touch (utimes)                     file: ATTRIB
 *   E after B/C the re-opened new file delivers an in-place write again
 * Exit status 0 means every case passed on every dir. Needs libWUtil's kqueue
 * backend (W_KQueueAddFilter != NULL); exits 2 otherwise.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/event.h>
#include <sys/time.h>

#ifndef O_EVTONLY
#define O_EVTONLY 0x00008000
#endif

extern int W_HandleInputEvents(int waitForInput, int inputfd);
extern void W_CheckTimerHandlers(void);
typedef void W_KQueueProc(const struct kevent *ev, void *clientData);
extern void *W_KQueueAddFilter(const struct kevent *kev, W_KQueueProc *proc, void *cd);
extern void W_KQueueDeleteFilter(void *id);

static int failures;

static long now_ms(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000L + tv.tv_usec / 1000;
}

typedef struct { int fd; void *id; unsigned got; long when; } W;
static W fw, dw;

static void on_ev(const struct kevent *ev, void *cd)
{
	W *w = cd;
	w->got |= ev->fflags;
	if (!w->when)
		w->when = now_ms();
}

static int watch(W *w, const char *path, unsigned fflags)
{
	struct kevent k;
	w->got = 0; w->when = 0;
	w->fd = open(path, O_EVTONLY | O_CLOEXEC);
	if (w->fd < 0) { printf("open(%s, O_EVTONLY): %s\n", path, strerror(errno)); return -1; }
	EV_SET(&k, w->fd, EVFILT_VNODE, EV_ADD | EV_CLEAR, fflags, 0, NULL);
	w->id = W_KQueueAddFilter(&k, on_ev, w);
	if (!w->id) { printf("W_KQueueAddFilter failed (select backend?)\n"); close(w->fd); w->fd = -1; return -2; }
	return 0;
}

static void unwatch(W *w)
{
	if (w->fd < 0) return;
	W_KQueueDeleteFilter(w->id);
	close(w->fd);
	w->fd = -1;
}

/* pump for up to ms, returning early once both wanted masks were seen */
static void pump(unsigned wantf, unsigned wantd, int ms)
{
	long end = now_ms() + ms;
	while (now_ms() < end) {
		W_CheckTimerHandlers();
		W_HandleInputEvents(0, -1);
		if ((fw.got & wantf) == wantf && (dw.got & wantd) == wantd && (wantf || wantd))
			break;
		usleep(2000);
	}
}

static const char *names(unsigned f, char *b)
{
	b[0] = 0;
	if (f & NOTE_DELETE) strcat(b, "DELETE ");
	if (f & NOTE_WRITE)  strcat(b, "WRITE ");
	if (f & NOTE_EXTEND) strcat(b, "EXTEND ");
	if (f & NOTE_ATTRIB) strcat(b, "ATTRIB ");
	if (f & NOTE_LINK)   strcat(b, "LINK ");
	if (f & NOTE_RENAME) strcat(b, "RENAME ");
	if (f & NOTE_REVOKE) strcat(b, "REVOKE ");
	if (!b[0]) strcpy(b, "-");
	return b;
}

static void report(const char *where, const char *name, int ok, long t0)
{
	char a[64], b[64];
	printf("%-4s %-28s %-6s file[%s] %ldms dir[%s] %ldms\n", ok ? "PASS" : "FAIL", where, name,
	       names(fw.got, a), fw.when ? fw.when - t0 : -1, names(dw.got, b), dw.when ? dw.when - t0 : -1);
	if (!ok) failures++;
}

static void putfile(const char *p, const char *s, int flags)
{
	int fd = open(p, O_WRONLY | flags, 0644);
	if (fd >= 0) { write(fd, s, strlen(s)); close(fd); }
}

#define FILE_NOTES (NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_LINK | NOTE_DELETE | NOTE_RENAME | NOTE_REVOKE)
#define DIR_NOTES (NOTE_WRITE | NOTE_DELETE | NOTE_RENAME | NOTE_LINK | NOTE_REVOKE)
#define WAIT 1500

static void run(const char *base)
{
	char d[512], f[600], t[600];
	long t0;
	int ok;

	snprintf(d, sizeof d, "%s/vnt.%d", base, (int)getpid());
	snprintf(f, sizeof f, "%s/WindowMaker", d);
	snprintf(t, sizeof t, "%s/WindowMaker.tmp", d);
	mkdir(d, 0755);
	putfile(f, "{ a = 1; }\n", O_CREAT | O_TRUNC);
	sync();

	if (watch(&dw, d, DIR_NOTES) || watch(&fw, f, FILE_NOTES)) { failures++; goto out; }
	pump(0, 0, 100);	/* settle */

	/* A */
	fw.got = dw.got = 0; fw.when = dw.when = 0; t0 = now_ms();
	putfile(f, "{ a = 2; }\n", O_APPEND);
	pump(NOTE_WRITE, 0, WAIT);
	report(base, "A in-place write", fw.got & (NOTE_WRITE | NOTE_EXTEND), t0);

	/* D */
	fw.got = dw.got = 0; fw.when = dw.when = 0; t0 = now_ms();
	utimes(f, NULL);
	pump(NOTE_ATTRIB, 0, WAIT);
	report(base, "D touch", fw.got & NOTE_ATTRIB, t0);

	/* B: atomic replace, then re-arm on the new vnode as wmaker does */
	fw.got = dw.got = 0; fw.when = dw.when = 0; t0 = now_ms();
	putfile(t, "{ a = 3; }\n", O_CREAT | O_TRUNC);
	rename(t, f);
	pump(NOTE_DELETE, NOTE_WRITE, WAIT);
	ok = (fw.got & (NOTE_DELETE | NOTE_RENAME)) && (dw.got & NOTE_WRITE);
	report(base, "B atomic rename-replace", ok, t0);
	unwatch(&fw);
	if (watch(&fw, f, FILE_NOTES)) { failures++; goto out; }

	/* E */
	fw.got = dw.got = 0; fw.when = dw.when = 0; t0 = now_ms();
	putfile(f, "{ a = 4; }\n", O_APPEND);
	pump(NOTE_WRITE, 0, WAIT);
	report(base, "E write after re-arm", fw.got & (NOTE_WRITE | NOTE_EXTEND), t0);

	/* C */
	fw.got = dw.got = 0; fw.when = dw.when = 0; t0 = now_ms();
	unlink(f);
	putfile(f, "{ a = 5; }\n", O_CREAT | O_TRUNC);
	pump(NOTE_DELETE, NOTE_WRITE, WAIT);
	ok = (fw.got & NOTE_DELETE) && (dw.got & NOTE_WRITE);
	report(base, "C delete + recreate", ok, t0);
out:
	unwatch(&fw);
	unwatch(&dw);
	unlink(f); unlink(t); rmdir(d);
}

int main(int argc, char **argv)
{
	int i;
	fw.fd = dw.fd = -1;
	if (argc > 1) {
		for (i = 1; i < argc; i++) run(argv[i]);
	} else {
		run("/tmp");
		run(getenv("HOME") ? getenv("HOME") : "/var/root");
	}
	printf(failures ? "FAILED: %d\n" : "ALL PASS\n", failures);
	return failures ? 1 : 0;
}
