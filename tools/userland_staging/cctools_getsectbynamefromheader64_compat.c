/* Real getsectbynamefromheader_64(), verbatim from
 * third_party/cctools/libmacho/getsecbyname.c:100-127 (real, pinned
 * cctools-973.0.1 source, same repo build_corefoundation.sh's own
 * cctools_arch.o already trial-compiles against). Extracted into its
 * own curated file rather than compiling getsecbyname.c wholesale:
 * that file also defines getsectbynamefromheaderwithswap()/_64(), which
 * pull in real swap_section()/swap_segment_command() from
 * libmacho/swap.c -- a real, but entirely unneeded, dependency chain
 * for this project's one real caller (launchd core.c never calls the
 * withswap variants). Same "curated extraction, not whole-file
 * inclusion" pattern this tier already uses for launchd_libc_extras.a/
 * cf_libc_extras.a. */
#include <mach-o/loader.h>
#include <string.h>

const struct section_64 *
getsectbynamefromheader_64(
struct mach_header_64 *mhp,
const char *segname,
const char *sectname)
{
	struct segment_command_64 *sgp;
	struct section_64 *sp;
	uint32_t i, j;

	sgp = (struct segment_command_64 *)
	      ((char *)mhp + sizeof(struct mach_header_64));
	for(i = 0; i < mhp->ncmds; i++){
	    if(sgp->cmd == LC_SEGMENT_64)
		if(strncmp(sgp->segname, segname, sizeof(sgp->segname)) == 0 ||
		   mhp->filetype == MH_OBJECT){
		    sp = (struct section_64 *)((char *)sgp +
			 sizeof(struct segment_command_64));
		    for(j = 0; j < sgp->nsects; j++){
			if(strncmp(sp->sectname, sectname,
			   sizeof(sp->sectname)) == 0 &&
			   strncmp(sp->segname, segname,
			   sizeof(sp->segname)) == 0)
			    return(sp);
			sp = (struct section_64 *)((char *)sp +
			     sizeof(struct section_64));
		    }
		}
	    sgp = (struct segment_command_64 *)((char *)sgp + sgp->cmdsize);
	}
	return((struct section_64 *)0);
}
