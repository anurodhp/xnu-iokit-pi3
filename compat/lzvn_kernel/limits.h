/*
 * limits.h for Apple's lzfse LZVN decoder in a kernel TU (DAR-331).
 *
 * third_party/lzfse/src/lzfse_internal.h:32 does `#include <limits.h>`.
 * The kernel build compiles with -nostdinc and xnu's EXTERNAL_HEADERS/
 * supplies only stdint.h/stddef.h/stdarg.h/stdbool.h/stdatomic.h -- no
 * limits.h; kernel code reaches the same constants through
 * <machine/limits.h> (-> bsd/arm/limits.h:73-88). Same redirect as
 * compat/xxhash_kernel/limits.h, kept separate so each include dir stays
 * scoped to its own objects. Redirect only -- defines nothing. On the
 * include path solely for the two LZVN objects
 * (tools/vendor_iographics_into_xnu.sh).
 */
#ifndef _COMPAT_LZVN_KERNEL_LIMITS_H
#define _COMPAT_LZVN_KERNEL_LIMITS_H
#include <machine/limits.h>
#endif
