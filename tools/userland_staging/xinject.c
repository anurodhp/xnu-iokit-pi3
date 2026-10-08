/* xinject: drive real mouse and keyboard input through the X server's XTEST extension, for testing
 * GUI apps on the device from a script (no xdotool on the image). Commands run in order:
 *
 *   xinject move X Y | click X Y [BUTTON] | dblclick X Y | down X Y [B] | up [B]   (B: 1 left, 2 middle, 3 right)
 *           drag X1 Y1 X2 Y2 [B] | key CHORD | type TEXT | sleep MS | where | windows | raise ID | lower ID
 *
 * CHORD is keysyms joined by '+', modifiers first: ctrl+n, shift+cmd+Delete, Return, Down.
 * Modifier names: ctrl, shift, alt, cmd (Mod1 on this X server, what GNUstep treats as Command), super.
 * Example: xinject click 300 200 sleep 200 key ctrl+c
 * SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel)
 */
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static Display *dpy;

static void
pause_ms(int ms)
{
	XFlush(dpy);
	usleep((useconds_t)ms * 1000);
}

static void
move(int x, int y)
{
	XTestFakeMotionEvent(dpy, -1, x, y, 0);
	pause_ms(20);
}

static void
button(int b, Bool down)
{
	XTestFakeButtonEvent(dpy, (unsigned)b, down, 0);
	pause_ms(30);
}

static void
tap_key(KeySym ks, Bool shift_needed)
{
	KeyCode kc = XKeysymToKeycode(dpy, ks);
	KeyCode sh = XKeysymToKeycode(dpy, XK_Shift_L);
	if (!kc) { fprintf(stderr, "xinject: no keycode for keysym 0x%lx\n", (unsigned long)ks); return; }
	if (shift_needed) XTestFakeKeyEvent(dpy, sh, True, 0);
	XTestFakeKeyEvent(dpy, kc, True, 0);
	XTestFakeKeyEvent(dpy, kc, False, 0);
	if (shift_needed) XTestFakeKeyEvent(dpy, sh, False, 0);
	pause_ms(25);
}

static KeySym
modifier_keysym(const char *name)
{
	if (!strcmp(name, "ctrl")) return XK_Control_L;
	if (!strcmp(name, "shift")) return XK_Shift_L;
	if (!strcmp(name, "alt") || !strcmp(name, "cmd")) return XK_Alt_L;	/* Mod1 */
	if (!strcmp(name, "super")) return XK_Super_L;
	return NoSymbol;
}

static void
chord(char *spec)
{
	KeySym mods[4];
	int nmods = 0, i;
	char *tok, *save = NULL, *last = NULL, *parts[8];
	int n = 0;
	for (tok = strtok_r(spec, "+", &save); tok && n < 8; tok = strtok_r(NULL, "+", &save)) parts[n++] = tok;
	if (!n) return;
	last = parts[n - 1];
	for (i = 0; i < n - 1 && nmods < 4; i++) {
		KeySym m = modifier_keysym(parts[i]);
		if (m == NoSymbol) { fprintf(stderr, "xinject: unknown modifier %s\n", parts[i]); return; }
		mods[nmods++] = m;
	}
	for (i = 0; i < nmods; i++) XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, mods[i]), True, 0);
	{
		KeySym ks = XStringToKeysym(last);
		if (ks == NoSymbol) fprintf(stderr, "xinject: unknown key %s\n", last);
		else tap_key(ks, False);
	}
	for (i = nmods - 1; i >= 0; i--) XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, mods[i]), False, 0);
	pause_ms(40);
}

static void
type_text(const char *s)
{
	for (; *s; s++) {
		KeySym ks;
		char one[2] = { *s, 0 };
		Bool shift = False;
		if (*s == ' ') ks = XK_space;
		else if (*s >= 'A' && *s <= 'Z') { one[0] = (char)(*s - 'A' + 'a'); ks = XStringToKeysym(one); shift = True; }
		else if (*s == '.') ks = XK_period;
		else if (*s == '/') ks = XK_slash;
		else if (*s == '-') ks = XK_minus;
		else if (*s == '_') { ks = XK_minus; shift = True; }
		else ks = XStringToKeysym(one);
		tap_key(ks, shift);
	}
}

/* print the window tree (two levels below the root): id, geometry, map state, override-redirect, name */
static void
dump_tree(Window w, int depth)
{
	Window root, parent, *kids = NULL;
	unsigned n = 0, i;
	XWindowAttributes a;
	char *name = NULL;
	if (!XGetWindowAttributes(dpy, w, &a)) return;
	XFetchName(dpy, w, &name);
	if (depth > 0)
		printf("%*s0x%lx %dx%d%+d%+d d%d vis0x%lx %s%s \"%s\"\n", depth * 2, "", w, a.width, a.height, a.x, a.y, a.depth, (unsigned long)XVisualIDFromVisual(a.visual),
		       a.map_state == IsViewable ? "viewable" : a.map_state == IsUnviewable ? "unviewable" : "unmapped",
		       a.override_redirect ? " OR" : "", name ? name : "");
	if (name) XFree(name);
	if (depth < 3 && XQueryTree(dpy, w, &root, &parent, &kids, &n)) {
		for (i = 0; i < n; i++) dump_tree(kids[i], depth + 1);
		if (kids) XFree(kids);
	}
}

int
main(int argc, char **argv)
{
	int i = 1;
	if (!(dpy = XOpenDisplay(NULL))) { fprintf(stderr, "xinject: cannot open display (set DISPLAY and XAUTHORITY)\n"); return 1; }
	{
		int ev, er, maj, min;
		if (!XTestQueryExtension(dpy, &ev, &er, &maj, &min)) { fprintf(stderr, "xinject: no XTEST extension\n"); return 1; }
	}
	while (i < argc) {
		const char *c = argv[i++];
		#define ARG(n) (i + (n) < argc ? atoi(argv[i + (n)]) : 0)
		if (!strcmp(c, "move") && i + 1 < argc) { move(ARG(0), ARG(1)); i += 2; }
		else if (!strcmp(c, "click") && i + 1 < argc) {
			int b = (i + 2 < argc && argv[i + 2][0] >= '1' && argv[i + 2][0] <= '9') ? ARG(2) : 1;
			move(ARG(0), ARG(1)); i += 2; if (i < argc && argv[i][0] >= '1' && argv[i][0] <= '9') i++;
			button(b, True); button(b, False);
		}
		else if (!strcmp(c, "dblclick") && i + 1 < argc) { move(ARG(0), ARG(1)); i += 2; button(1, True); button(1, False); pause_ms(60); button(1, True); button(1, False); }
		else if (!strcmp(c, "down") && i + 1 < argc) {
			int b = (i + 2 < argc && argv[i + 2][0] >= '1' && argv[i + 2][0] <= '9') ? ARG(2) : 1;
			move(ARG(0), ARG(1)); i += 2; if (i < argc && argv[i][0] >= '1' && argv[i][0] <= '9') i++;
			button(b, True);
		}
		else if (!strcmp(c, "up")) {
			int b = (i < argc && argv[i][0] >= '1' && argv[i][0] <= '9') ? atoi(argv[i++]) : 1;
			button(b, False);
		}
		else if (!strcmp(c, "drag") && i + 3 < argc) {
			int x1 = ARG(0), y1 = ARG(1), x2 = ARG(2), y2 = ARG(3), s;
			i += 4;
			move(x1, y1); button(1, True); pause_ms(80);
			for (s = 1; s <= 20; s++) { move(x1 + (x2 - x1) * s / 20, y1 + (y2 - y1) * s / 20); pause_ms(15); }
			pause_ms(120); button(1, False);
		}
		else if (!strcmp(c, "key") && i < argc) { char *spec = strdup(argv[i++]); chord(spec); free(spec); }
		else if (!strcmp(c, "type") && i < argc) { type_text(argv[i++]); }
		else if (!strcmp(c, "sleep") && i < argc) { pause_ms(atoi(argv[i++])); }
		else if (!strcmp(c, "raise") && i < argc) { XRaiseWindow(dpy, (Window)strtoul(argv[i++], NULL, 0)); pause_ms(100); }
		else if (!strcmp(c, "lower") && i < argc) { XLowerWindow(dpy, (Window)strtoul(argv[i++], NULL, 0)); pause_ms(100); }
		else if (!strcmp(c, "windows")) { dump_tree(DefaultRootWindow(dpy), 0); }
		else if (!strcmp(c, "where")) {
			Window r, ch; int rx, ry, wx, wy; unsigned m;
			XQueryPointer(dpy, DefaultRootWindow(dpy), &r, &ch, &rx, &ry, &wx, &wy, &m);
			printf("%d %d\n", rx, ry);
		}
		else { fprintf(stderr, "xinject: bad command or arguments: %s\n", c); return 2; }
	}
	XFlush(dpy);
	XCloseDisplay(dpy);
	return 0;
}
