/* Mach-O-native replacement for real FreeBSD's <sys/sys/linker_set.h>
 * (DAR-339, su(1)'s real static-PAM-module support).
 *
 * WHY THIS IS CUSTOM CODE, NOT A PORT: real FreeBSD's own linker_set.h
 * (checked directly against this project's pinned third_party/
 * freebsd-src @ release/13.2.0) implements DATA_SET/SET_DECLARE/
 * SET_FOREACH via `__section("set_" #set)` (a SINGLE ELF-style section
 * string) plus ELF's own `__start_set_X`/`__stop_set_X` linker-
 * synthesized boundary symbols. Neither half survives on Mach-O,
 * confirmed via real, standalone tests against this project's own
 * Xcode-12 toolchain (arm64-apple-ios14.4), not assumed:
 *   1. `__attribute__((section("set_foo")))` (single string) fails to
 *      compile at all: "mach-o section specifier requires a segment and
 *      section separated by a comma".
 *   2. Even with a real two-part Mach-O section name, ld64's
 *      `section$start$SEG$SECT`/`section$end$SEG$SECT` magic-symbol
 *      convention (the closest analog to ELF's boundary symbols) does
 *      NOT auto-synthesize on this toolchain -- a real link left both
 *      symbols undefined.
 * Apple's own OpenPAM header (openpam.h) already reflects exactly this:
 * `NO_STATIC_MODULES` is unconditionally forced on any non-`__FreeBSD__`
 * target. Per this project's source-priority rule (CLAUDE.md), custom
 * code is the correct choice here -- neither real upstream source
 * works as-is on Mach-O, checked directly rather than guessed.
 *
 * REAL WORKING MECHANISM USED INSTEAD: `getsectiondata()` (Apple's own
 * real cctools libmacho, third_party/cctools/libmacho/getsecbyname.c,
 * extracted verbatim into su_getsectiondata_cctools.c -- see that file's
 * own header) against the running executable's own Mach-O header
 * (`&_mh_execute_header`, <mach-o/ldsyms.h>), which DOES work -- proven
 * by a real compile+link test against this project's own Csu start.s/
 * crt.c + dynamic dylib set before this file was written. Each `sym`
 * registered via DATA_SET is placed as a real pointer value inside a
 * dedicated `__DATA,__pam_sm` section; SET_FOREACH walks that section's
 * real bounds at runtime via getsectiondata(), the same real
 * begin/size contract ELF's boundary-symbol pair gives real FreeBSD.
 *
 * SCOPE, HONESTLY NARROWED: real FreeBSD's linker_set.h is fully generic
 * over any number of differently-named sets. This one is NOT -- Mach-O
 * section names are hard-capped at 16 characters by the tools (checked
 * directly: a real compile of a `set`-name-derived section string, e.g.
 * "__ls__openpam_static_modules", failed with "mach-o section specifier
 * uses an unknown section type"), so every set defined via this header
 * shares the SAME literal Mach-O section ("__DATA,__pam_sm") regardless
 * of its FreeBSD-API `set` name. This is safe for this project's real,
 * actual usage -- openpam.h/openpam_static.c define and iterate exactly
 * ONE real linker set in this entire codebase, `_openpam_static_modules`
 * (confirmed via exhaustive grep) -- but a second, differently-typed
 * linker set introduced later would collide with this one's entries and
 * needs a second, distinctly-named section (and a matching
 * `__ls_begin`/`__ls_end` pair) added here, not silently reused.
 */
#ifndef _SU_LINKER_SET_H_
#define _SU_LINKER_SET_H_

#define __ls_CONCAT1(x, y) x ## y
#define __ls_CONCAT(x, y) __ls_CONCAT1(x, y)

/* Real getsectiondata()-backed bounds lookup (su_getsectiondata_cctools.c
 * for the real function body; su_linker_set_bounds.c for these two real,
 * narrow wrappers -- see that file's own header). */
extern void *__ls_begin(const char *segname, const char *sectname);
extern void *__ls_end(const char *segname, const char *sectname);

/*
 * DATA_SET(set, sym): register &(sym) into the one real Mach-O section
 * this header uses for every set (see this file's own "SCOPE" note
 * above). Matches real FreeBSD's own DATA_SET contract: a set holds
 * pointers TO the real objects, not the objects themselves.
 */
#define DATA_SET(set, sym)						\
	static const void * __ls_CONCAT(__ls_entry_, sym)		\
	__attribute__((used, section("__DATA,__pam_sm")))		\
	= &(sym)

/*
 * SET_DECLARE(set, ptype): real FreeBSD declares the boundary-symbol
 * externs here. This header needs none (bounds are looked up at
 * runtime by name, not by boundary symbol) -- expands to a harmless,
 * always-valid file-scope extern declaration purely so the real,
 * unmodified call site's trailing `;` stays syntactically valid.
 */
#define SET_DECLARE(set, ptype)					\
	extern char __ls_CONCAT(__ls_declare_, set)

/*
 * SET_FOREACH(pvar, set): real FreeBSD contract preserved exactly --
 * pvar must be declared `ptype **pvar` by the caller; each iteration's
 * `*pvar` is one real registered object's address. `set` is unused here
 * (see this file's own "SCOPE" note) but kept as a macro parameter to
 * match every other real FreeBSD linker_set.h call site's real, textual
 * shape (openpam_static.c's own `SET_FOREACH(module,
 * _openpam_static_modules)` needs no edits).
 */
#define SET_FOREACH(pvar, set)						\
	for ((pvar) = (__typeof__(pvar))__ls_begin("__DATA", "__pam_sm"); \
	     (pvar) < (__typeof__(pvar))__ls_end("__DATA", "__pam_sm"); \
	     (pvar)++)

#endif
