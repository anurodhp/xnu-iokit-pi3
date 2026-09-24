/*
 * assert.h for Apple's lzfse LZVN decoder in a kernel TU (DAR-331).
 *
 * third_party/lzfse/src/lzfse_fse.h:28 includes <assert.h>; its assert()
 * calls sit in inline bit-field helpers (lzfse_fse.h:193-215,
 * lzfse_internal.h:478-499). The kernel's -nostdinc include path has no
 * <assert.h>; its assert() is <kern/assert.h> (osfmk/kern/assert.h:93-109).
 * Redirect only -- defines nothing. On the include path solely for the
 * two LZVN objects (tools/vendor_iographics_into_xnu.sh).
 */
#ifndef _COMPAT_LZVN_KERNEL_ASSERT_H
#define _COMPAT_LZVN_KERNEL_ASSERT_H
#include <kern/assert.h>
#endif
