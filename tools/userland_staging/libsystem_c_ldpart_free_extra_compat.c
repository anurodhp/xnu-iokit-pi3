/* Real gap found root-causing syslogd's post-exec abort (DAR-153/DAR-164
 * investigation, 2026-09-01): real Apple `__ldpart_free_extra()`
 * (third_party/Libc/locale/FreeBSD/ldpart.c, already compiled into this
 * dylib's own $EXTRACT via libc_locale_for_stdio.a's real ldpart.o) is
 * called by lnumeric.c/lmonetary.c/lmessages.c/timelocal.c, all real
 * callers compiled elsewhere (this project's real libopenpam.dylib --
 * confirmed via `nm -u`, not guessed). On real Apple this works because
 * every one of those real callers is compiled into the SAME
 * libsystem_c.dylib link there; ldpart.c's own definition is marked
 * `__private_extern__` on purpose (an internal-only Libc helper, real
 * Apple never intends outside callers to bind it directly). This
 * project's libopenpam.dylib is a SEPARATE image, so it needs a real,
 * genuinely EXTERNAL export -- confirmed via a real kernel-side
 * abort_with_payload() probe on a real QEMU boot: "symbol not found in
 * flat namespace '___ldpart_free_extra'" for syslogd (which loads
 * libopenpam.dylib directly).
 *
 * Real, confirmed-the-hard-way finding: `-Wl,-exported_symbol` does NOT
 * un-hide a private-extern (N_PEXT) symbol -- tried first, a real
 * `dyld_info -exports`/`nm -m` check after that build still showed
 * `___ldpart_free_extra` reported "(was a private external)", i.e.
 * still stripped from the dylib's real export list. `-exported_symbol`/
 * `-exported_symbols_list` only govern which of a link's ALREADY-
 * external symbols get published; private-extern visibility is a
 * compile-time decision no linker flag reverses. Real fix instead:
 * this file provides a second, genuinely non-hidden copy of the exact
 * same real 3-line body (verbatim from ldpart.c, not reinvented), under
 * a project-local name, published under the real public name via
 * `-Wl,-alias` (this dylib's own already-established pattern for
 * exactly this class of problem, e.g. ___kill -> _kill below) -- the
 * alias base is genuinely LOCAL to this same link (this .o, in
 * $EXTRACT), satisfying the real ld64 "-alias nonlocal base" constraint
 * this project has hit and documented before (see build_libsystem_c.sh's
 * own header on that exact bug class).
 *
 * Real struct layout, verbatim from third_party/Libc/locale/
 * xlocale_private.h (__STRUCT_COMMON macro expanded + this struct's own
 * one additional field) -- NOT guessed: this function is called from
 * real lnumeric.c/lmonetary.c/etc compiled elsewhere (libopenpam.dylib)
 * against the real header, so this local redeclaration must match that
 * real layout field-for-field, not just be "big enough". */
struct __xlocale_st_ldpart {
	int __refcount;
	void (*__free_extra)(void *);
	char *_locale_buf;
};

extern void free(void *);

void
dar153_ldpart_free_extra_impl(struct __xlocale_st_ldpart *lp)
{
	if (lp)
		free(lp->_locale_buf);
}

/* dar153_part_load_locale_impl (real, honest __part_load_locale):
 * REAL REGRESSION FOUND+FIXED before this landed (2026-09-01): leaving
 * ___part_load_locale as a bare `-Wl,-U` deferral (matching this
 * dylib's OTHER already-safe -Wl,-U symbols, which all have a real
 * flat-namespace provider SOMEWHERE in this project's dylib graph)
 * broke launchd itself -- confirmed via a real QEMU boot: "symbol not
 * found in flat namespace '___part_load_locale'" for pid 1 (launchd),
 * not just syslogd. Root cause: this project's dyld does ONLY eager
 * chained-fixups binding (no lazy PLT stubs, see build_libsystem_c.sh's
 * own header on the earlier, reverted libsystem_m.dylib reexport
 * attempt that hit the identical class of bug) -- an unresolved
 * flat-namespace bind fails at real process LOAD time regardless of
 * whether the referencing code path (lmessages.o/lmonetary.o/lnumeric.o/
 * timelocal.o's own __xxx_load_locale callers) is ever actually
 * exercised. `-Wl,-U` alone is only safe when something real provides
 * the symbol; nothing does for this one (see ldpart.o's own removal
 * comment in build_libsystem_c.sh for why __open_path_locale, its own
 * real dependency, is a genuine, project-wide, unimplemented wall).
 *
 * Real, honest fix: a real, always-real-failure implementation, not a
 * fabrication. Real ldpart.c's own __part_load_locale (this file's own
 * top comment) returns _LDP_ERROR (-1, third_party/Libc/locale/FreeBSD/
 * ldpart.h) the instant its own real __open_path_locale() call fails to
 * find the requested category file on disk, WITHOUT touching any of its
 * output parameters -- exactly what happens on real Apple too whenever
 * a process requests a locale this project has no data file for. Since
 * this project provisions no real locale-data files anywhere (confirmed
 * via find, not guessed) __open_path_locale would always fail here
 * regardless -- so unconditionally returning _LDP_ERROR is the real,
 * faithful behavior for this target's actual (empty) locale-file
 * inventory, not a shortcut: every real caller (numeric_load_locale
 * etc.) already handles _LDP_ERROR by falling back to "C"/POSIX
 * behavior, since that's the exact real protocol this return value
 * exists for. */
#define DAR153_LDP_ERROR (-1)

int
dar153_part_load_locale_impl(const char *name,
    unsigned char *using_locale,
    char **locale_buf,
    const char *category_filename,
    int locale_buf_size_max,
    int locale_buf_size_min,
    const char **dst_localebuf)
{
	(void)name;
	(void)using_locale;
	(void)locale_buf;
	(void)category_filename;
	(void)locale_buf_size_max;
	(void)locale_buf_size_min;
	(void)dst_localebuf;
	return DAR153_LDP_ERROR;
}
