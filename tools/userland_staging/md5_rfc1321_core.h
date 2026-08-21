/* Userland-owned MD5 core (state/count/buffer + Init/Update/Final), used
 * only by md5_commoncrypto_shim.c -- see that file's header comment for
 * why this exists (Phase 4.6 item 6: real CommonCrypto-shaped digest API
 * for the `md5` command) and md5_rfc1321_core.c's own header for exactly
 * why this is a last-resort custom implementation rather than real Apple
 * or FreeBSD source.
 *
 * Deliberately a separate, userland-local struct/prototype set (NOT a
 * `#include` of third_party/xnu-7195/libkern/libkern/crypto/md5.h) --
 * that header is real Apple source but lives under a kernel-only include
 * path (libkern/crypto/) this project's userland CFLAGS never adds, and
 * pulling it in would create a real, undesirable userland-build
 * dependency on kernel headers for no benefit (the layout below is
 * byte-for-byte identical anyway).
 */
#ifndef _MD5_RFC1321_CORE_H_
#define _MD5_RFC1321_CORE_H_

#include <sys/types.h>

#define MD5_RFC1321_DIGEST_LENGTH 16

typedef struct {
    uint32_t state[4];       /* state (ABCD) */
    uint32_t count[2];       /* number of bits, modulo 2^64 (lsb first) */
    unsigned char buffer[64]; /* input buffer */
} MD5_CTX;

void MD5Init(MD5_CTX *ctx);
void MD5Update(MD5_CTX *ctx, const void *data, unsigned int len);
void MD5Final(unsigned char digest[MD5_RFC1321_DIGEST_LENGTH], MD5_CTX *ctx);

#endif /* _MD5_RFC1321_CORE_H_ */
