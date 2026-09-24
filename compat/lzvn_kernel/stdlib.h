/*
 * stdlib.h for Apple's lzfse LZVN decoder in a kernel TU (DAR-331).
 *
 * third_party/lzfse/src/lzfse_fse.h:31 includes <stdlib.h>, but nothing
 * the LZVN decoder compiles uses anything from it: no malloc/free/abort/
 * exit/qsort call appears in lzvn_decode_base.[ch], lzfse_internal.h,
 * lzfse_fse.h or lzfse_tunables.h (checked with grep, not assumed). The
 * kernel has no <stdlib.h> under -nostdinc, so this is intentionally
 * empty. If a future pin starts using a stdlib function, the build fails
 * on an undeclared identifier rather than silently resolving it.
 * On the include path solely for the two LZVN objects.
 */
#ifndef _COMPAT_LZVN_KERNEL_STDLIB_H
#define _COMPAT_LZVN_KERNEL_STDLIB_H
#endif
