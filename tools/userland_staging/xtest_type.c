/*
 * xtest_type: type text into the focused window of an X display through the
 * XTEST extension (libXtst's XTestFakeKeyEvent), for driving the xdm greeter
 * in QEMU boot tests without a keyboard.
 *
 *   xtest_type [-w seconds] TEXT...
 *
 * Each argument is typed in turn; a Return key is sent after each one, so
 *   xtest_type root darwin
 * answers the greeter's login and password prompts. The display comes from
 * $DISPLAY and the cookie from $XAUTHORITY (for an xdm-owned server, the
 * file under /var/lib/xdm/authdir/authfiles). -w retries XOpenDisplay once a
 * second for up to that many seconds, for a server that is still starting.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

static void key(Display *dpy, KeySym sym)
{
    KeyCode kc = XKeysymToKeycode(dpy, sym);
    int shift = 0;

    if (kc == 0) {
        fprintf(stderr, "xtest_type: no keycode for keysym 0x%lx\n", (unsigned long)sym);
        return;
    }
    /* The keysym is in column 1 of its keycode's row: needs Shift. */
    if (XkbKeycodeToKeysym(dpy, kc, 0, 0) != sym &&
        XkbKeycodeToKeysym(dpy, kc, 0, 1) == sym)
        shift = 1;
    if (shift)
        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Shift_L), True, 0);
    XTestFakeKeyEvent(dpy, kc, True, 0);
    XTestFakeKeyEvent(dpy, kc, False, 0);
    if (shift)
        XTestFakeKeyEvent(dpy, XKeysymToKeycode(dpy, XK_Shift_L), False, 0);
    XSync(dpy, False);
    usleep(30000);
}

int main(int argc, char **argv)
{
    Display *dpy = NULL;
    int wait = 0, i, ev, er, maj, min;

    if (argc > 2 && strcmp(argv[1], "-w") == 0) {
        wait = atoi(argv[2]);
        argv += 2;
        argc -= 2;
    }
    if (argc < 2) {
        fprintf(stderr, "usage: xtest_type [-w seconds] TEXT...\n");
        return 2;
    }
    for (i = 0; ; i++) {
        dpy = XOpenDisplay(NULL);
        if (dpy || i >= wait)
            break;
        sleep(1);
    }
    if (!dpy) {
        fprintf(stderr, "xtest_type: cannot open display %s\n",
                getenv("DISPLAY") ? getenv("DISPLAY") : "(unset)");
        return 1;
    }
    if (!XTestQueryExtension(dpy, &ev, &er, &maj, &min)) {
        fprintf(stderr, "xtest_type: server has no XTEST extension\n");
        return 1;
    }
    for (i = 1; i < argc; i++) {
        const char *p;
        for (p = argv[i]; *p; p++) {
            char s[2] = { *p, 0 };
            KeySym sym = (*p == ' ') ? XK_space : XStringToKeysym(s);
            if (sym == NoSymbol) {
                fprintf(stderr, "xtest_type: no keysym for '%c'\n", *p);
                continue;
            }
            key(dpy, sym);
        }
        key(dpy, XK_Return);
        usleep(300000);
    }
    XCloseDisplay(dpy);
    return 0;
}
