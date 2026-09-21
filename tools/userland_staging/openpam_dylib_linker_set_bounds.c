/* __ls_begin()/__ls_end(): real getsectiondata()-backed bounds lookup
 * for su_compat_include/linker_set.h's own SET_FOREACH, same real
 * mechanism as su_linker_set_bounds.c (see that file's own header for
 * the full DAR-339 background) -- but for libopenpam.dylib's own build,
 * not su(1)'s.
 *
 * Real bug found 2026-09-21 (a from-scratch build_everything.sh run):
 * build_openpam_dylib.sh's wholesale `ar x libopenpam.a` pulls in
 * su_linker_set_bounds.o (compiled into libopenpam.a for su(1)'s sake -
 * see build_openpam.sh), whose own &_mh_execute_header reference is
 * real and correct for su.macho (an MH_EXECUTE image, where the linker
 * always populates that symbol) but genuinely undefined when the same
 * object is linked into libopenpam.dylib instead: real Apple
 * <mach-o/ldsyms.h> is explicit that _mh_execute_header "does not
 * appear in any file type other than a MH_EXECUTE file type" - an
 * MH_DYLIB never gets it, confirmed by the real link failure this file
 * fixes ("libopenpam.dylib link found real undefined symbols:
 * __mh_execute_header").
 *
 * The real fix is not to drop __ls_begin()/__ls_end() from the dylib -
 * openpam_static.c's own real SET_DECLARE/SET_FOREACH usage (the actual
 * static-PAM-module registry OpenPAM's real openpam_dispatch() walks)
 * needs them there too, confirmed by nm -u on the extracted
 * openpam_static.o. It is to give the DYLIB its own real header
 * reference: &_mh_dylib_header, the exact MH_DYLIB counterpart to
 * _mh_execute_header (same <mach-o/ldsyms.h>, "does not appear in any
 * file type other than a MH_DYLIB file type... private to the code in
 * the library it is a part of" - precisely this case), linker-
 * synthesized the same way for any real MH_DYLIB output. This file
 * replaces su_linker_set_bounds.o in build_openpam_dylib.sh's object
 * list; su.macho keeps using the original, unmodified
 * su_linker_set_bounds.c/&_mh_execute_header, which stays correct for
 * an executable image.
 */
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>

extern unsigned char *getsectiondata(const struct mach_header_64 *,
    const char *, const char *, unsigned long *);

void *
__ls_begin(const char *segname, const char *sectname)
{
	unsigned long size = 0;
	const struct mach_header_64 *mh =
	    (const struct mach_header_64 *)&_mh_dylib_header;

	return (void *)getsectiondata(mh, segname, sectname, &size);
}

void *
__ls_end(const char *segname, const char *sectname)
{
	unsigned long size = 0;
	const struct mach_header_64 *mh =
	    (const struct mach_header_64 *)&_mh_dylib_header;
	unsigned char *base = getsectiondata(mh, segname, sectname, &size);

	if (base == 0)
		return (void *)0;
	return (void *)(base + size);
}
