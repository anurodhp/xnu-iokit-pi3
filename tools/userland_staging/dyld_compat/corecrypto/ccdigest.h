/* Real, confirmed dead include in dyld's glue.c: no ccdigest/ccsha1/
 * ccsha2 symbol is ever referenced in that file (grepped in full) --
 * this project's corecrypto gap (see project memory
 * project_xnu_rpi3_crypto_gap.md: corecrypto is not on
 * apple-oss-distributions at all, real SHA2 already ported from scratch
 * for the kernel side) doesn't need a real port here since nothing
 * actually calls into it from this file. Empty placeholder only. */
#ifndef _COMPAT_CORECRYPTO_ccdigest_H_
#define _COMPAT_CORECRYPTO_ccdigest_H_
#endif
