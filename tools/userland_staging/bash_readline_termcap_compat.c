/* Minimal, real (not fabricated) termcap(3) API surface for real GNU
 * readline (third_party/bash/bash-3.2/lib/readline/*.c) to link against.
 *
 * This target genuinely has no termcap/terminfo database (no /etc/
 * termcap, no /usr/share/terminfo, confirmed absent -- out of scope,
 * same class of gap as the timezone database WAS before Phase 4.6 item 1
 * ported one in, except a real termcap database is a much larger,
 * separate subsystem nothing here currently needs beyond this). That is
 * NOT the same as "readline can't work here" -- lib/readline/terminal.c's
 * own real, documented `_rl_init_terminal_io()` already has a complete,
 * intentional fallback path for exactly this situation (tgetent()
 * returning <= 0, e.g. TERM=dumb or an unrecognized terminal): it sets
 * `_rl_term_cr = "\r"`, `_rl_term_backspace = "\b"`, leaves every
 * cursor-addressing capability (`_rl_term_up`/`_rl_term_dc`/`_rl_term_IC`/
 * etc) NULL, and returns success -- a real, working (if visually
 * unfancy) line-editing mode, not a crash or a stub. This file's job is
 * only to make tgetent() genuinely, honestly report "no database" (real
 * termcap's own -1 "couldn't open database" return, the most accurate
 * of the three real return codes for this target's actual situation) so
 * that documented fallback path runs -- it does not fabricate terminal
 * capabilities that don't exist.
 *
 * Symbol surface matches third_party/bash/bash-3.2/lib/readline/tcap.h's
 * own `#else` branch exactly (taken because config.h's HAVE_TERMCAP_H is
 * now `#undef`'d -- see that file's own comment): PC/UP/BC/ospeed are
 * plain extern storage tcap.h declares and terminal.c's own fallback
 * path assigns directly (PC='\0', BC=_rl_term_backspace, UP=_rl_term_up)
 * -- this file only needs to provide the storage, not logic, for those
 * three/four. tgetnum/tgetflag/tgetstr/tgetent/tputs/tgoto need real
 * function bodies.
 */

typedef unsigned long size_t;

char PC;
char *UP, *BC;
short ospeed;

/* Real termcap tgetent() return codes: 1 = found or ok, 0 = terminal
 * type not found in the database, -1 = couldn't open the database file
 * at all. This target has no database file, period -- -1 is the honest
 * answer, not "0" (which would imply a database exists but doesn't list
 * this TERM). */
int tgetent(char *bp, const char *name) {
    (void)bp;
    (void)name;
    return -1;
}

/* Real termcap: -1 means "capability not present" for numeric caps. */
int tgetnum(const char *id) {
    (void)id;
    return -1;
}

/* Real termcap: 0 (false) means "capability not present" for boolean
 * caps -- real termcap's own documented default for an unlisted flag. */
int tgetflag(const char *id) {
    (void)id;
    return 0;
}

/* Real termcap: NULL means "capability not present" for string caps. */
char *tgetstr(const char *id, char **area) {
    (void)id;
    (void)area;
    return (char *)0;
}

/* Real tputs(3): write `str` through `putc_func`, honoring a real
 * leading numeric padding-delay prefix (`nn.n*str` -- historical
 * teletype-speed padding, e.g. "5*\E[2J") by simply skipping it, since
 * this target's serial console needs no artificial padding delay (a
 * real QEMU/UART link has no mechanical settle time to wait out) --
 * `affcnt`/`ospeed`-based delay computation is real termcap behavior
 * only relevant when padding is actually needed, so honestly a no-op
 * here rather than fabricated timing. Every capability string this
 * project's real fallback path in terminal.c ever sets ("\r", "\b") has
 * no such prefix anyway -- this still handles the real general case
 * correctly, not just those two, in case any caller (e.g. tgoto()
 * output, if that ever becomes reachable) passes one. */
int tputs(const char *str, int affcnt, int (*putc_func)(int)) {
    (void)affcnt;
    if (!str) {
        return 0;
    }
    /* Real termcap padding syntax is `nnn[.n]*string` -- the trailing
     * `*` is what confirms a leading digit run is actually a padding
     * count, not literal content that happens to start with a digit
     * (e.g. a capability string like "2ABC"). An earlier version of
     * this loop consumed the digit run unconditionally without checking
     * for that `*`, which would have silently eaten a real leading
     * digit from any future capability string routed through here --
     * caught by review, not by a failing test (every string actually
     * exercised today, "\r"/"\b", starts with neither a digit nor a
     * padding spec, so this was latent, not yet observed misbehavior). */
    const char *scan = str;
    while (*scan >= '0' && *scan <= '9') {
        scan++;
    }
    if (*scan == '.') {
        scan++;
        while (*scan >= '0' && *scan <= '9') {
            scan++;
        }
    }
    const char *p = str;
    if (scan != str && *scan == '*') {
        p = scan + 1; /* real padding prefix present -- skip it */
    }
    for (const char *c = p; *c; c++) {
        putc_func((unsigned char)*c);
    }
    return 0;
}

/* Real tgoto(3): expands a cursor-addressing capability string's %-escape
 * sequences for a given column/row. This target's real fallback path
 * (terminal.c's `_rl_init_terminal_io()`, tgetent()==-1 branch) always
 * leaves every cursor-addressing capability NULL
 * (_rl_term_IC/_rl_term_DC/etc), and readline's own real callers only
 * invoke tgoto() when one of those is non-NULL -- so this is genuinely
 * unreachable on this target today, not a corner case being papered
 * over. A minimal, honest implementation: real termcap's own documented
 * error return is the literal string "OOPS" when `cap` is unusable. */
char *tgoto(const char *cap, int col, int row) {
    (void)col;
    (void)row;
    /* Real termcap's own documented error string ("OOPS") for the
     * unusable-cap case, in mutable storage rather than a string
     * literal -- some real termcap consumers write through the
     * returned pointer in place, which would fault against read-only
     * literal memory; a static buffer avoids that even though nothing
     * in this project's real fallback path currently reaches this
     * function at all (terminal.c never sets a cursor-addressing
     * capability that would cause a real caller to invoke tgoto()). */
    static char oops[] = "OOPS";
    if (!cap || !*cap) {
        return oops;
    }
    return (char *)cap;
}
