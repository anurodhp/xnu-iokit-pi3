/* __ls_begin()/__ls_end(): real getsectiondata()-backed bounds lookup
 * for su_compat_include/linker_set.h's own SET_FOREACH (DAR-339). See
 * that file's own header for why this exists instead of a real
 * linker_set.h port, and su_getsectiondata_cctools.c's own header for
 * getsectiondata()'s real citation (Apple's own cctools libmacho).
 *
 * &_mh_execute_header (<mach-o/ldsyms.h>) is the running MAIN
 * executable's own compiled-in Mach-O header -- populated by the static
 * linker for any real MH_EXECUTE output, not runtime/crt-dependent.
 * su(1) links its real static-PAM-module registrations directly into
 * its own executable image (no dylib involved), so this is always the
 * right header to query.
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
	    (const struct mach_header_64 *)&_mh_execute_header;

	return (void *)getsectiondata(mh, segname, sectname, &size);
}

void *
__ls_end(const char *segname, const char *sectname)
{
	unsigned long size = 0;
	const struct mach_header_64 *mh =
	    (const struct mach_header_64 *)&_mh_execute_header;
	unsigned char *base = getsectiondata(mh, segname, sectname, &size);

	if (base == 0)
		return (void *)0;
	return (void *)(base + size);
}
