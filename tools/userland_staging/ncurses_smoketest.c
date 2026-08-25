/* Minimal real ncurses consumer -- proves libncurses.a's real public API
 * (initscr/printw/mvprintw/refresh/endwin) resolves and links cleanly
 * against this project's curated static libc stack, same
 * link/otool/nm-based structural smoketest every sibling build_*.sh in
 * this directory uses (see build_libc_regex.sh/build_libiconv.sh) -- the
 * resulting freestanding arm64 Mach-O can't actually execute() on this
 * host (wrong platform load command, no dyld, no code signature), same
 * as every other tier's *_smoketest.macho; real behavioral verification
 * only happens under QEMU as a real PID-1/exec'd command, out of scope
 * for this link-level check. */
#include <curses.h>

int main(void) {
    WINDOW *w = initscr();
    if (w != NULL) {
        printw("hello from real ncurses on iokit\n");
        mvprintw(1, 0, "row 1");
        refresh();
        endwin();
    }
    return 0;
}
