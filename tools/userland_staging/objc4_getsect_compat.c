/*
 * DAR-236 (full objc4 runtime port): real, from-scratch implementations
 * of getsectiondata()/getsegmentdata()/getsectdatafromheader_64() --
 * the real, public <mach-o/getsect.h> API objc-file.mm (getDataSection<T>,
 * used to find each loaded image's __DATA,__objc_classlist/__objc_catlist/
 * __objc_protolist/etc sections), objc-class.mm (linker-set section
 * bounds) and objc-block-trampolines.mm (the trampoline pages'
 * __TEXT,__objc_treg section) all call directly.
 *
 * Real root cause: this project's pinned third_party/cctools DOES carry
 * a real, unmodified implementation (libmacho/getsecbyname.c), but that
 * file also pulls in cctools' broader RLD/dyld-bind/32-bit-fat-fallback
 * machinery (crt_externs.h, _dyld_lookup_and_bind(), OPENSTEP
 * compatibility branches) this project's userland has no use for and
 * has never built any of before. Rather than staging that whole legacy
 * file's transitive dependencies for 3 functions, this is a real,
 * narrow, from-scratch reimplementation of the same well-documented,
 * public, stable on-disk Mach-O load-command format -- exactly the
 * precedent tools/userland_staging/libunwind_dyld_shim.c already
 * established in this project for the same class of problem
 * (__TEXT,__eh_frame lookup via direct LC_SEGMENT_64 walking instead of
 * relying on a getsectiondata() this target doesn't otherwise provide).
 * Real signatures confirmed against
 * third_party/cctools/include/mach-o/getsect.h, not guessed.
 */
#include <stdint.h>
#include <string.h>
#include <mach-o/loader.h>

uint8_t *
getsectiondata(const struct mach_header_64 *mhp, const char *segname,
               const char *sectname, unsigned long *size)
{
	const struct load_command *lc;
	uint32_t i;

	if (size != NULL)
		*size = 0;
	if (mhp == NULL || mhp->magic != MH_MAGIC_64)
		return NULL;

	lc = (const struct load_command *)((const uint8_t *)mhp + sizeof(struct mach_header_64));
	for (i = 0; i < mhp->ncmds; i++) {
		if (lc->cmd == LC_SEGMENT_64) {
			const struct segment_command_64 *sg = (const struct segment_command_64 *)lc;
			if (strncmp(sg->segname, segname, sizeof(sg->segname)) == 0) {
				const struct section_64 *sect =
				    (const struct section_64 *)((const uint8_t *)sg + sizeof(struct segment_command_64));
				uint32_t s;
				for (s = 0; s < sg->nsects; s++, sect++) {
					if (strncmp(sect->sectname, sectname, sizeof(sect->sectname)) == 0) {
						if (size != NULL)
							*size = (unsigned long)sect->size;
						if (sect->size == 0)
							return NULL;
						return (uint8_t *)((uintptr_t)mhp + sect->offset);
					}
				}
			}
		}
		lc = (const struct load_command *)((const uint8_t *)lc + lc->cmdsize);
	}
	return NULL;
}

uint8_t *
getsegmentdata(const struct mach_header_64 *mhp, const char *segname,
               unsigned long *size)
{
	const struct load_command *lc;
	uint32_t i;

	if (size != NULL)
		*size = 0;
	if (mhp == NULL || mhp->magic != MH_MAGIC_64)
		return NULL;

	lc = (const struct load_command *)((const uint8_t *)mhp + sizeof(struct mach_header_64));
	for (i = 0; i < mhp->ncmds; i++) {
		if (lc->cmd == LC_SEGMENT_64) {
			const struct segment_command_64 *sg = (const struct segment_command_64 *)lc;
			if (strncmp(sg->segname, segname, sizeof(sg->segname)) == 0) {
				if (size != NULL)
					*size = (unsigned long)sg->vmsize;
				return (uint8_t *)((uintptr_t)mhp + sg->fileoff);
			}
		}
		lc = (const struct load_command *)((const uint8_t *)lc + lc->cmdsize);
	}
	return NULL;
}

char *
getsectdatafromheader_64(const struct mach_header_64 *mhp, const char *segname,
                          const char *sectname, uint64_t *size)
{
	unsigned long sz = 0;
	uint8_t *data = getsectiondata(mhp, segname, sectname, &sz);
	if (size != NULL)
		*size = (uint64_t)sz;
	return (char *)data;
}
