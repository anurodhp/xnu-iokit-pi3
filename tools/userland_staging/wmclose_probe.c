/*
 * wmclose_probe (DAR-452): drives and inspects Window Maker's window
 * Close/Kill paths from a shell, since this image ships no xdotool/xkill.
 *
 *   wmclose_probe list
 *       every managed client window (root _NET_CLIENT_LIST, which Window
 *       Maker maintains -- third_party/wmaker/src/wmspec.c
 *       updateClientList), with its WM_CLASS, _NET_WM_PID, WM_PROTOCOLS
 *       and the frame geometry.
 *   (<class-substr> below also matches WM_NAME, or is a "0x..." window id)
 *   wmclose_probe delete <class-substr>
 *       send WM_PROTOCOLS/WM_DELETE_WINDOW exactly as Window Maker's
 *       wClientSendProtocol does (third_party/wmaker/src/client.c:256).
 *   wmclose_probe xkill <class-substr>
 *       XKillClient(client window), exactly as wClientKill does
 *       (client.c:273).
 *   wmclose_probe click <x> <y> [ctrl] [b3]
 *       XTest: move the pointer to (x,y) and click button 1 (optionally
 *       with Control held -- window.c:3109's "control-click kills").
 *   wmclose_probe state
 *       the pointer's modifier mask, XKB mod state and the modifier map
 *       (which keys drive Shift..Mod5).
 *   wmclose_probe keylog <seconds>
 *       grab the keyboard and print each key event's keycode/keysym/state.
 *   wmclose_probe key <keysym>
 *       XTest press+release of that key (e.g. Return, Escape).
 *   wmclose_probe titlebar <class-substr> [ctrl] [b3]
 *       XTest click on the middle of the frame's titlebar; b3 opens
 *       Window Maker's window menu (window.c titlebarMouseDown).
 *   wmclose_probe closebtn <class-substr> [ctrl] [b3]
 *       XTest click on the right-hand titlebar button of that client's
 *       frame (Window Maker's close button: frame width minus half a
 *       titlebar height, half a titlebar height below the frame top).
 *
 * Display from $DISPLAY (default ":0"). Exit 0 on success.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <X11/XKBlib.h>

static Display *dpy;

static long get_card32(Window w, const char *name, int *ok)
{
    Atom a = XInternAtom(dpy, name, False), type;
    int fmt;
    unsigned long n, after;
    unsigned char *data = NULL;
    long v = 0;
    *ok = 0;
    if (XGetWindowProperty(dpy, w, a, 0, 1, False, XA_CARDINAL, &type, &fmt,
                           &n, &after, &data) == Success && data && n == 1) {
        v = ((long *)data)[0];
        *ok = 1;
    }
    if (data)
        XFree(data);
    return v;
}

static int client_list(Window **out)
{
    Atom a = XInternAtom(dpy, "_NET_CLIENT_LIST", False), type;
    int fmt;
    unsigned long n, after;
    unsigned char *data = NULL;
    if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), a, 0, 1024, False,
                           XA_WINDOW, &type, &fmt, &n, &after, &data) != Success || !data)
        return 0;
    *out = (Window *)data;
    return (int)n;
}

static void class_of(Window w, char *buf, size_t len)
{
    XClassHint ch = {0};
    buf[0] = 0;
    if (XGetClassHint(dpy, w, &ch)) {
        snprintf(buf, len, "%s.%s", ch.res_name ? ch.res_name : "",
                 ch.res_class ? ch.res_class : "");
        if (ch.res_name) XFree(ch.res_name);
        if (ch.res_class) XFree(ch.res_class);
    }
}

/* Top-level ancestor (Window Maker's frame) of w. */
static Window frame_of(Window w)
{
    Window root, parent, *kids;
    unsigned int nk;
    for (;;) {
        if (!XQueryTree(dpy, w, &root, &parent, &kids, &nk))
            return w;
        if (kids)
            XFree(kids);
        if (parent == root)
            return w;
        w = parent;
    }
}

/* <sub> matches WM_CLASS or WM_NAME as a substring, or is a window id
 * ("0x..."). With several matches the LAST one in _NET_CLIENT_LIST wins
 * (the most recently mapped window). */
static Window find(const char *sub)
{
    Window *l = NULL, hit = 0;
    int n = client_list(&l), i;
    char cls[256];
    if (!strncmp(sub, "0x", 2))
        return (Window)strtoul(sub, NULL, 16);
    for (i = 0; i < n; i++) {
        char *nm = NULL;
        int m;
        class_of(l[i], cls, sizeof cls);
        XFetchName(dpy, l[i], &nm);
        m = strstr(cls, sub) || (nm && strstr(nm, sub));
        if (nm)
            XFree(nm);
        if (m) {
            hit = l[i];
        }
    }
    if (l)
        XFree(l);
    if (!hit)
        fprintf(stderr, "wmclose_probe: no client matching '%s'\n", sub);
    return hit;
}

static void list(void)
{
    Window *l = NULL;
    int n = client_list(&l), i, j, ok;
    char cls[256];
    printf("wmclose_probe: %d managed clients\n", n);
    for (i = 0; i < n; i++) {
        Atom *prot = NULL;
        int np = 0;
        long pid = get_card32(l[i], "_NET_WM_PID", &ok);
        XWindowAttributes fa;
        Window f = frame_of(l[i]);
        char *nm = NULL;
        class_of(l[i], cls, sizeof cls);
        XFetchName(dpy, l[i], &nm);
        XGetWindowAttributes(dpy, f, &fa);
        printf("  win=0x%lx name='%s' class=%s pid=%s%ld frame=0x%lx %dx%d+%d+%d protocols:",
               l[i], nm ? nm : "", cls, ok ? "" : "(none)", pid, f, fa.width, fa.height, fa.x, fa.y);
        if (XGetWMProtocols(dpy, l[i], &prot, &np)) {
            for (j = 0; j < np; j++) {
                char *nm = XGetAtomName(dpy, prot[j]);
                printf(" %s", nm);
                XFree(nm);
            }
            XFree(prot);
        }
        printf("\n");
        if (nm)
            XFree(nm);
    }
    if (l)
        XFree(l);
}

static void click(int x, int y, int ctrl, int button)
{
    KeyCode kc = XKeysymToKeycode(dpy, XK_Control_L);
    XTestFakeMotionEvent(dpy, -1, x, y, 0);
    XSync(dpy, False);
    if (!button) {
        /* "move": position the pointer only, so a real (QEMU monitor
         * mouse_button) click through the Xorg input driver lands there. */
        printf("wmclose_probe: pointer moved to (%d,%d)\n", x, y);
        return;
    }
    usleep(200000);
    if (ctrl)
        XTestFakeKeyEvent(dpy, kc, True, 0);
    XTestFakeButtonEvent(dpy, button, True, 0);
    XSync(dpy, False);
    usleep(100000);
    XTestFakeButtonEvent(dpy, button, False, 0);
    if (ctrl)
        XTestFakeKeyEvent(dpy, kc, False, 0);
    XSync(dpy, False);
    printf("wmclose_probe: clicked button %d at (%d,%d)%s\n", button, x, y, ctrl ? " with Control" : "");
}

/* Trailing options of click/titlebar/closebtn: "ctrl" holds Control, "b3"
 * uses button 3, "move" only moves the pointer there. */
static void opts(int argc, char **argv, int from, int *ctrl, int *button)
{
    int i;
    *ctrl = 0;
    *button = 1;
    for (i = from; i < argc; i++) {
        if (!strcmp(argv[i], "ctrl"))
            *ctrl = 1;
        else if (!strcmp(argv[i], "b3"))
            *button = 3;
        else if (!strcmp(argv[i], "move"))
            *button = 0;
    }
}

int main(int argc, char **argv)
{
    const char *d = getenv("DISPLAY");
    Window w;
    if (argc < 2) {
        fprintf(stderr, "usage: wmclose_probe list|state|keylog N|delete C|xkill C|key K|click X Y [ctrl] [b3|move]|titlebar C [ctrl] [b3|move]|closebtn C [ctrl] [b3|move]\n");
        return 2;
    }
    dpy = XOpenDisplay(d ? d : ":0");
    if (!dpy) {
        fprintf(stderr, "wmclose_probe: cannot open display\n");
        return 1;
    }
    if (!strcmp(argv[1], "list")) {
        list();
    } else if (!strcmp(argv[1], "delete") && argc > 2) {
        XEvent ev;
        if (!(w = find(argv[2])))
            return 1;
        memset(&ev, 0, sizeof ev);
        ev.xclient.type = ClientMessage;
        ev.xclient.message_type = XInternAtom(dpy, "WM_PROTOCOLS", False);
        ev.xclient.format = 32;
        ev.xclient.window = w;
        ev.xclient.data.l[0] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
        ev.xclient.data.l[1] = CurrentTime;
        XSendEvent(dpy, w, False, NoEventMask, &ev);
        XSync(dpy, False);
        printf("wmclose_probe: sent WM_DELETE_WINDOW to 0x%lx\n", w);
    } else if (!strcmp(argv[1], "xkill") && argc > 2) {
        if (!(w = find(argv[2])))
            return 1;
        XKillClient(dpy, w);
        XSync(dpy, False);
        printf("wmclose_probe: XKillClient(0x%lx) done\n", w);
    } else if (!strcmp(argv[1], "click") && argc > 3) {
        int ctrl, b;
        opts(argc, argv, 4, &ctrl, &b);
        click(atoi(argv[2]), atoi(argv[3]), ctrl, b);
    } else if (!strcmp(argv[1], "state")) {
        /* The modifier state Window Maker sees in a button event: window.c
         * windowCloseClick (close button) and titlebarMouseDown (button-3
         * window menu) act only when (state & modifiers_mask) == 0. */
        Window r, c;
        int rx, ry, wx, wy, i, j;
        unsigned int mask;
        XkbStateRec xs;
        XModifierKeymap *mm = XGetModifierMapping(dpy);
        static const char *mn[8] = {"Shift", "Lock", "Control", "Mod1", "Mod2", "Mod3", "Mod4", "Mod5"};
        XQueryPointer(dpy, DefaultRootWindow(dpy), &r, &c, &rx, &ry, &wx, &wy, &mask);
        printf("wmclose_probe: pointer state mask=0x%x\n", mask);
        if (XkbGetState(dpy, XkbUseCoreKbd, &xs) == Success)
            printf("wmclose_probe: xkb mods=0x%x base=0x%x latched=0x%x locked=0x%x group=%d\n",
                   xs.mods, xs.base_mods, xs.latched_mods, xs.locked_mods, xs.group);
        for (i = 0; mm && i < 8; i++) {
            printf("  %-7s:", mn[i]);
            for (j = 0; j < mm->max_keypermod; j++) {
                KeyCode kc = mm->modifiermap[i * mm->max_keypermod + j];
                if (kc) {
                    KeySym ks = XkbKeycodeToKeysym(dpy, kc, 0, 0);
                    printf(" %d(%s)", kc, ks ? XKeysymToString(ks) : "?");
                }
            }
            printf("\n");
        }
        if (mm)
            XFreeModifiermap(mm);
        {
            Atom a = XInternAtom(dpy, "_XKB_RULES_NAMES", False), type;
            int fmt;
            unsigned long n, after, k;
            unsigned char *data = NULL;
            if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), a, 0, 1024, False, XA_STRING,
                                   &type, &fmt, &n, &after, &data) == Success && data) {
                printf("wmclose_probe: _XKB_RULES_NAMES:");
                for (k = 0; k < n; k++)
                    putchar(data[k] ? data[k] : '|');
                printf("\n");
                XFree(data);
            }
        }
    } else if (!strcmp(argv[1], "keylog") && argc > 2) {
        /* Grab the keyboard for N seconds and print every key event's
         * keycode, keysym and modifier state. */
        time_t end = time(NULL) + atoi(argv[2]);
        if (XGrabKeyboard(dpy, DefaultRootWindow(dpy), False, GrabModeAsync, GrabModeAsync,
                          CurrentTime) != GrabSuccess) {
            fprintf(stderr, "wmclose_probe: keyboard grab failed\n");
            return 1;
        }
        printf("wmclose_probe: keylog for %s s\n", argv[2]);
        fflush(stdout);
        while (time(NULL) < end) {
            while (XPending(dpy)) {
                XEvent ev;
                XNextEvent(dpy, &ev);
                if (ev.type == KeyPress || ev.type == KeyRelease) {
                    KeySym ks = XkbKeycodeToKeysym(dpy, ev.xkey.keycode, 0, 0);
                    printf("  %s keycode=%u keysym=%s state=0x%x\n",
                           ev.type == KeyPress ? "press  " : "release", ev.xkey.keycode,
                           ks ? XKeysymToString(ks) : "NoSymbol", ev.xkey.state);
                    fflush(stdout);
                }
            }
            usleep(20000);
        }
        XUngrabKeyboard(dpy, CurrentTime);
    } else if (!strcmp(argv[1], "key") && argc > 2) {
        KeyCode kc = XKeysymToKeycode(dpy, XStringToKeysym(argv[2]));
        if (!kc) {
            fprintf(stderr, "wmclose_probe: no keycode for '%s'\n", argv[2]);
            return 1;
        }
        XTestFakeKeyEvent(dpy, kc, True, 0);
        XSync(dpy, False);
        usleep(50000);
        XTestFakeKeyEvent(dpy, kc, False, 0);
        XSync(dpy, False);
        printf("wmclose_probe: pressed %s (keycode %d)\n", argv[2], kc);
    } else if (!strcmp(argv[1], "closebtn") && argc > 2) {
        XWindowAttributes fa, ca;
        Window f, child;
        int cx, cy, tb, ctrl, b;
        if (!(w = find(argv[2])))
            return 1;
        f = frame_of(w);
        XGetWindowAttributes(dpy, f, &fa);
        XGetWindowAttributes(dpy, w, &ca);
        XTranslateCoordinates(dpy, w, f, 0, 0, &cx, &cy, &child);
        tb = cy > 4 ? cy : 20;  /* titlebar height = client's y offset in frame */
        opts(argc, argv, 3, &ctrl, &b);
        click(fa.x + fa.width - tb / 2 - 1, fa.y + tb / 2, ctrl, b);
    } else if (!strcmp(argv[1], "titlebar") && argc > 2) {
        /* click the middle of the frame's titlebar ("b3" opens the window menu) */
        XWindowAttributes fa;
        Window f, child;
        int cx, cy, ctrl, b;
        if (!(w = find(argv[2])))
            return 1;
        f = frame_of(w);
        XGetWindowAttributes(dpy, f, &fa);
        XTranslateCoordinates(dpy, w, f, 0, 0, &cx, &cy, &child);
        opts(argc, argv, 3, &ctrl, &b);
        click(fa.x + fa.width / 2, fa.y + (cy > 4 ? cy : 20) / 2, ctrl, b);
    } else {
        fprintf(stderr, "wmclose_probe: bad arguments\n");
        return 2;
    }
    XCloseDisplay(dpy);
    return 0;
}
