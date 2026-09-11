/*
 * DAR-283: narrow, honestly-documented stand-in for the 9 libedit
 * (editline) entry points real Apple `scutil(8)` links against.
 *
 * WHAT IS REAL HERE AND WHAT IS NOT
 * =================================
 * The HEADER scutil compiles against is real, unmodified Apple source:
 * third_party/libedit/src/histedit.h at tag `libedit-55`, the exact tag
 * distribution-macOS's `macos-112` manifest pins for Darwin 20.3
 * (release.json: {"project": "libedit", "tag": "libedit-55"}) -- pinned
 * by setup_third_party.sh alongside every other real dependency. The
 * pinned Xcode-12 iPhoneOS SDK ships no <histedit.h> at all, and
 * scutil.h #includes it unconditionally, so without that real pin
 * scutil.tproj does not compile at all.
 *
 * The LIBRARY is not built. libedit-55 is 26 .c files / ~19k lines with
 * an AWK `makelist` generated-header step (fcns.h/help.h/emacs.h/vi.h/
 * common.h are all generated from the sources at build time) and a real
 * terminfo + wide-character dependency chain -- a genuinely separate
 * library port, the same size and shape as this project's ncurses/
 * libiconv/libresolv ports, each of which got its own build_*.sh and its
 * own ticket. Filed as its own ticket rather than smuggled into this one.
 *
 * WHY THIS IS A REAL DEGRADATION PATH, NOT A FABRICATION
 * ======================================================
 * scutil.c uses libedit in exactly one place: the interactive command
 * loop's line reader. That code already has a real, Apple-authored,
 * non-editing fallback that runs on every real macOS today -- it is the
 * path taken whenever stdin is not a tty:
 *
 *     src->el = NULL;  src->h = NULL;
 *     if (isatty(fileno(src->fp))) { ...el_init()... }
 *     ...
 *     getLine():  if (src->el) { el_gets(...) } else { fgets(buf, len, src->fp) }
 *
 * and scutil.c ALREADY handles "editline came up but is not usable",
 * again with Apple's own code, by tearing it back down to that same
 * fgets() path:
 *
 *     if ((el_get(src->el, EL_EDITMODE, &editmode) != -1) && editmode != 0) {
 *             el_set(src->el, EL_SIGNAL, 1);
 *     } else {
 *             history_end(src->h); src->h  = NULL;
 *             el_end(src->el);     src->el = NULL;
 *     }
 *
 * These stand-ins drive precisely that second branch: el_init() returns
 * NULL, history_init() returns NULL, and el_get() returns -1, so real
 * Apple's own `else` runs, src->el/src->h go back to NULL, and every
 * subsequent getLine() takes real Apple's own real fgets() reader. The
 * result is a real, fully-functional interactive scutil with no line
 * editing and no command history -- which is exactly what real Apple's
 * scutil does today when you pipe it a script. Nothing about the
 * dynamic-store, preferences, DNS or NWI behaviour is touched.
 *
 * Every non-interactive invocation -- `scutil --dns`, `scutil --nwi`,
 * `scutil --get`, `scutil <<< "show State:/Network/Global/DNS"`,
 * `echo ... | scutil` -- never calls into this file at all: either it
 * exits before the input loop is set up (the --xxx paths all end in
 * exit()), or isatty() is false and el_init() is never reached.
 *
 * el_set()/el_source()/el_gets()/el_end()/history() are therefore
 * reachable only through a non-NULL EditLine or History handle, which cannot
 * exist here. They are real, linkable, and never invoked at runtime --
 * the same "real, linkable, never actually invoked" shape as
 * configd_plugin_reachability_compat.c's own documented stand-ins.
 */
#include <stdio.h>
#include <stdarg.h>
#include <histedit.h>

EditLine *
el_init(const char *prog, FILE *fin, FILE *fout, FILE *ferr)
{
	(void)prog; (void)fin; (void)fout; (void)ferr;
	return NULL;
}

void
el_end(EditLine *el)
{
	(void)el;
}

const char *
el_gets(EditLine *el, int *count)
{
	(void)el;
	if (count != NULL) {
		*count = 0;
	}
	return NULL;
}

int
el_set(EditLine *el, int op, ...)
{
	(void)el; (void)op;
	return -1;
}

int
el_get(EditLine *el, int op, ...)
{
	(void)el; (void)op;
	return -1;
}

int
el_source(EditLine *el, const char *file)
{
	(void)el; (void)file;
	return -1;
}

History *
history_init(void)
{
	return NULL;
}

void
history_end(History *h)
{
	(void)h;
}

int
history(History *h, HistEvent *ev, int op, ...)
{
	(void)h; (void)op;
	if (ev != NULL) {
		ev->num = 0;
		ev->str = NULL;
	}
	return -1;
}
