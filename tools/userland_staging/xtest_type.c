/*
 * xtest_type: type text into the focused window of an X display through the
 * XTEST extension (libXtst's XTestFakeKeyEvent), for driving the login window
 * in QEMU boot tests without a keyboard.
 *
 *   xtest_type [-w seconds] TEXT...
 *   xtest_type [-w seconds] -c X Y [-o]
 *
 * Each argument is typed in turn; a Return key is sent after each one, so
 *   xtest_type root darwin
 * answers the greeter's login and password prompts. The display comes from
 * $DISPLAY and the cookie from $XAUTHORITY (for gsdm's server,
 * /var/lib/gsdm/auth-0). -c clicks button 1 at screen position X,Y instead
 * (XTestFakeMotionEvent + XTestFakeButtonEvent), holding Alt_L with -o: X's
 * Mod1 on every keymap (Xvfb's has no Alt_R), the PC keyboard's Option key,
 * i.e. an Option-click (used to screenshot
 * Naples' menu bar panels on a virtual display). -w retries XOpenDisplay once a
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
        fprintf(stderr, "usage: xtest_type [-w seconds] TEXT... | -c X Y [-o]\n");
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
    if (strcmp(argv[1], "-c") == 0 && argc < 4) {
        fprintf(stderr, "usage: xtest_type [-w seconds] -c X Y [-o]\n");
        return 2;
    }
    if (argc >= 4 && strcmp(argv[1], "-c") == 0) {
        int x = atoi(argv[2]), y = atoi(argv[3]);
        int option = argc >= 5 && strcmp(argv[4], "-o") == 0;
        KeyCode alt = XKeysymToKeycode(dpy, XK_Alt_L);
        XTestFakeMotionEvent(dpy, -1, x, y, 0);
        XSync(dpy, False);
        usleep(100000);
        if (option && alt)
            XTestFakeKeyEvent(dpy, alt, True, 0);
        XTestFakeButtonEvent(dpy, 1, True, 0);
        XSync(dpy, False);
        usleep(60000);
        XTestFakeButtonEvent(dpy, 1, False, 0);
        if (option && alt)
            XTestFakeKeyEvent(dpy, alt, False, 0);
        XSync(dpy, False);
        XCloseDisplay(dpy);
        return 0;
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
