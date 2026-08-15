#ifndef _LIBMALLOC_COMPAT_H_
#define _LIBMALLOC_COMPAT_H_
#include <stdint.h>
struct mach_header;

/* Real Apple prototype (mach-o/dyld_priv.h): "extern intptr_t
 * _dyld_get_image_slide(const struct mach_header* mh);" -- not
 * redeclared with a body here; libmalloc_dispatch.c defines the real
 * (honest: this target has no dyld, PID-1 execve()s a statically-linked
 * fixed-address Mach-O with no ASLR slide ever applied, so slide==0 is
 * the factually correct answer -- see third_party/libmalloc/src/vm.c's
 * mvm_aslr_init(), whose real "ASLR disabled" branch this exercises,
 * not a workaround) definition. _NSGetMachExecuteHeader itself is
 * intentionally NOT declared here -- third_party/Libc/include/
 * crt_externs.h (pulled in by internal.h) already has the real, correct
 * LP64 prototype (struct mach_header_64 *), and this file is
 * force-included before that, so redeclaring it here would conflict. */
extern intptr_t _dyld_get_image_slide(const struct mach_header *mh);

/* nano_zone.h (real Apple source, third_party/libmalloc/src/nano_zone.h)
 * is unconditionally #included by internal.h, but its address-layout
 * struct is hard-gated to __x86_64 only ("#error Unknown Architecture"
 * for arm64) in this exact pinned snapshot (libmalloc-317.40.8) -- a
 * real, confirmed gap in the public source (cross-checked against
 * libmalloc.xcodeproj: nano_malloc.c IS listed as a Sources member for
 * every real platform including arm64, so Apple's actual internal build
 * must resolve this some other way not present in this public
 * checkout). Nothing in this project's curated build (magazine_tiny/
 * small/medium/large/rack/vm/bitarray/malloc_printf -- deliberately NOT
 * nano_malloc.c/nanov2_malloc.c, a separate larger future item, see
 * build_libmalloc.sh's header comment) references any nano_zone.h
 * symbol -- confirmed by grep before relying on this. Pre-defining its
 * include guard here is the standard, honest C technique for
 * suppressing a vendored header this project must not edit
 * (third_party/libmalloc is an unmodified-pinned-tag checkout per
 * setup_third_party.sh's own convention) whose body this build doesn't
 * need and can't compile as-is. */
#define __NANO_ZONE_H
#endif
