/*
 * limits.h for xxhash.h in a kernel translation unit (DAR-331).
 *
 * xxhash.h (v0.8.2) :2091 unconditionally does `#include <limits.h>`.
 * The kernel build compiles with -nostdinc and xnu's EXTERNAL_HEADERS/
 * supplies only stdint.h/stddef.h/stdarg.h/stdbool.h/stdatomic.h -- no
 * limits.h; kernel code reaches the same constants through
 * <machine/limits.h> (-> bsd/arm/limits.h:73-88: UINT_MAX, ULONG_MAX,
 * ULLONG_MAX, which is all xxhash.h:525-527,828 reads). This file only
 * redirects to that real header; it defines nothing.
 *
 * Kept in its own directory, on the include path only for the
 * IOGraphics sources (tools/iographics_gap_audit.sh), so it cannot
 * shadow anything elsewhere in the kernel.
 */
#ifndef _COMPAT_XXHASH_KERNEL_LIMITS_H
#define _COMPAT_XXHASH_KERNEL_LIMITS_H
#include <machine/limits.h>
#endif
