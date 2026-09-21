/* Real, unmodified (arm64/__LP64__ body) getsectiondata()/getsegmentdata()
 * from Apple's own cctools libmacho (third_party/cctools/libmacho/
 * getsecbyname.c), extracted for DAR-339 (su(1)'s real static-PAM-module
 * linker-set replacement).
 *
 * WHY THIS FILE EXISTS: OpenPAM's real static-module support
 * (openpam_static.c's own SET_DECLARE/SET_FOREACH over
 * _openpam_static_modules) needs a way to find the bounds of a custom
 * linker-collected array. Real FreeBSD's own mechanism for this
 * (sys/sys/linker_set.h: `__start_set_X`/`__stop_set_X` boundary symbols,
 * ELF-only) does not carry over to Mach-O -- confirmed directly, not
 * assumed: a real standalone link test of ld64's own
 * `section$start$SEG$SECT`/`section$end$SEG$SECT` magic-symbol
 * convention (the closest analog) left both symbols undefined on this
 * project's actual toolchain. `getsectiondata()` against the running
 * image's own Mach-O header is the real, working alternative -- proven
 * via a real compile+link test against this project's own Csu start.s/
 * crt.c + dynamic dylib set before this file was written (see DAR-339's
 * own Linear ticket for that test's real output).
 *
 * Only the __LP64__ bodies are pulled -- this project's real target is
 * always arm64 (__LP64__ always defined), so the 32-bit `#if
 * !defined(__LP64__)` branch (real upstream's first `getsectiondata()`
 * overload, same file, earlier in it) is dead code here and dropped.
 * The file's OTHER real functions (`getsectbyname()`,
 * `getsectbynamefromheaderwithswap[_64]()`, the OPENSTEP-era
 * `_dyld_lookup_and_bind` compat shims) are also dropped -- a real,
 * standalone link test of the whole file left `_swap_section[_64]`/
 * `_swap_segment_command[_64]` undefined (needed only by the *withswap*
 * variants, which su's own PAM linker-set support never calls) -- same
 * narrow "only pull the real function bodies actually needed" extraction
 * already established by pw_pw_util_freebsd.c (DAR-341) for a different
 * real upstream file.
 *
 * Both function bodies below are byte-for-byte the real upstream file,
 * uncommented boilerplate aside.
 */
#include <stdint.h>
#include <string.h>
#include <mach-o/loader.h>

uint8_t *
getsectiondata(
const struct mach_header_64 *mhp,
const char *segname,
const char *sectname,
unsigned long *size)
{
    struct segment_command_64 *sgp;
    struct section_64 *sp;
    uint32_t i, j;
    intptr_t slide;

	slide = 0;
	sp = 0;
	sgp = (struct segment_command_64 *)
	      ((char *)mhp + sizeof(struct mach_header_64));
	for(i = 0; i < mhp->ncmds; i++){
	    if(sgp->cmd == LC_SEGMENT_64){
		if(strcmp(sgp->segname, "__TEXT") == 0){
		    slide = (uintptr_t)mhp - sgp->vmaddr;
		}
		if(strncmp(sgp->segname, segname, sizeof(sgp->segname)) == 0){
		    sp = (struct section_64 *)((char *)sgp +
			 sizeof(struct segment_command_64));
		    for(j = 0; j < sgp->nsects; j++){
			if(strncmp(sp->sectname, sectname,
			   sizeof(sp->sectname)) == 0 &&
			   strncmp(sp->segname, segname,
			   sizeof(sp->segname)) == 0){
			    *size = sp->size;
			    return((uint8_t *)(sp->addr) + slide);
			}
			sp = (struct section_64 *)((char *)sp +
			     sizeof(struct section_64));
		    }
		}
	    }
	    sgp = (struct segment_command_64 *)((char *)sgp + sgp->cmdsize);
	}
	return(0);
}

uint8_t *
getsegmentdata(
const struct mach_header_64 *mhp,
const char *segname,
unsigned long *size)
{
    struct segment_command_64 *sgp;
    intptr_t slide;
    uint32_t i;

	slide = 0;
	sgp = (struct segment_command_64 *)
	      ((char *)mhp + sizeof(struct mach_header_64));
	for(i = 0; i < mhp->ncmds; i++){
	    if(sgp->cmd == LC_SEGMENT_64){
		if(strcmp(sgp->segname, "__TEXT") == 0){
		    slide = (uintptr_t)mhp - sgp->vmaddr;
		}
		if(strncmp(sgp->segname, segname, sizeof(sgp->segname)) == 0){
		    *size = sgp->vmsize;
		    return((uint8_t *)(sgp->vmaddr + slide));
		}
	    }
	    sgp = (struct segment_command_64 *)((char *)sgp + sgp->cmdsize);
	}
	return(0);
}
