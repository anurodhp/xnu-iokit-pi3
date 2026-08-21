/*
 * md5_rfc1321_core.c - standalone userland MD5, backing the real
 * CommonCrypto-shaped digest API shim (md5_commoncrypto_shim.c) that
 * real, unmodified third_party/text_cmds/md5/md5.c links against
 * (docs/roadmap.md Phase 4.6 item 6).
 *
 * Source-priority investigation (see CLAUDE.md's "Verify claims against
 * source" convention -- this was actually checked, not assumed from
 * prior knowledge):
 *
 *  1. Real Apple md5(1) source: third_party/text_cmds/md5/md5.c, pinned
 *     at tag text_cmds-106 -- confirmed against apple-oss-distributions/
 *     distribution-macOS's macos-112 manifest (`git ls-tree macos-112 --
 *     text_cmds` == commit 26b743953867382d59dfe09b9011dde610797e1a ==
 *     text_cmds-106 exactly). Its real sibling commoncrypto.c/.h in the
 *     same directory (also real Apple source, same tag) is the "official"
 *     glue, but its Digest_File() is built on dispatch_io/dispatch_
 *     semaphore/GCD queues -- this project's own
 *     tools/userland_staging/README.md already documents libdispatch as
 *     investigated and ruled out project-wide ("a real monolith, not
 *     separable file-by-file"), so that exact file can't be used
 *     unmodified here; md5_commoncrypto_shim.c reimplements the same
 *     Digest_End/Digest_Data/Digest_File surface with a plain blocking
 *     read() loop instead (see that file's header).
 *
 *  2. Real corecrypto MD5 core (what CommonCrypto's own CC_MD5_ and
 *     CCDigest family of functions actually call through to on real macOS):
 *     added third_party/CommonCrypto (tag CommonCrypto-60178.40.2, also
 *     confirmed against the macos-112 manifest) to get the real, public
 *     CommonDigest.h/CommonDigestSPI.h headers this shim's CCDigestCtx/
 *     CCDigestAlg/CC_MD5_DIGEST_LENGTH definitions come from verbatim --
 *     but CommonCrypto's own lib/CommonDigest.c is a thin wrapper that
 *     `#include`s <corecrypto/ccmd5.h>/<corecrypto/cc_priv.h> and calls
 *     ccdigest_init/update/final; the actual compression-function
 *     implementation (corecrypto itself) is NOT present anywhere in this
 *     checkout -- confirmed by grep: this CommonCrypto clone doesn't
 *     vendor a corecrypto/ source tree at all, and
 *     third_party/xnu-7195/EXTERNAL_HEADERS/corecrypto/ (the only other
 *     place corecrypto could plausibly live in this project) has just
 *     four headers (cc.h, cc_config.h, ccmode_impl.h, ccpad.h) with zero
 *     ccdigest.c/ccmd5.c/ccsha2.c bodies. Apple has genuinely never
 *     open-sourced corecrypto's digest cores -- not guessed, checked.
 *
 *  3. FreeBSD fallback precedent: no FreeBSD-sourced libmd (MD5Init/
 *     MD5Update/MD5Final) is already vendored anywhere in this checkout
 *     (grepped third_party/ for MD5Init -- zero hits before this file).
 *
 *  4. Last resort, and the one actually used here: this project already
 *     wrote and kernel-boot-verified a from-scratch, direct RFC 1321
 *     implementation once before, for an unrelated reason -- xnu-7195's
 *     own libkern/crypto/corecrypto_md5.c (bsd/msdosfs's real root-mount
 *     UUID generation needed a kernel-side MD5 body when g_crypto_funcs,
 *     the real corecrypto-kext registration table, was never populated
 *     on this from-scratch kernel; see that file's own header for the
 *     full story). This file is a direct, line-for-line-equivalent port
 *     of that exact same, already-proven algorithm to a userland-owned
 *     MD5_CTX (see md5_rfc1321_core.h) -- reusing a real, tested
 *     implementation this project already committed to, rather than
 *     writing a fresh one from the RFC text a second time.
 */

#include "md5_rfc1321_core.h"

#include <string.h>

static const unsigned char PADDING[64] = { 0x80 /* rest is zero */ };

static inline uint32_t
rotl32(uint32_t x, int n)
{
    return (x << n) | (x >> (32 - n));
}

#define F(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & ~(z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | ~(z)))

static const uint32_t K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
    0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};

static const int Sshift[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

static void
md5_compress(MD5_CTX *ctx, const unsigned char block[64])
{
    uint32_t x[16];
    for (int i = 0; i < 16; i++) {
        x[i] = (uint32_t)block[i * 4] |
            ((uint32_t)block[i * 4 + 1] << 8) |
            ((uint32_t)block[i * 4 + 2] << 16) |
            ((uint32_t)block[i * 4 + 3] << 24);
    }

    uint32_t a = ctx->state[0], b = ctx->state[1];
    uint32_t c = ctx->state[2], d = ctx->state[3];

    for (int i = 0; i < 64; i++) {
        uint32_t f;
        int g;
        if (i < 16) {
            f = F(b, c, d);
            g = i;
        } else if (i < 32) {
            f = G(b, c, d);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = H(b, c, d);
            g = (3 * i + 5) % 16;
        } else {
            f = I(b, c, d);
            g = (7 * i) % 16;
        }
        uint32_t temp = d;
        d = c;
        c = b;
        b = rotl32(a + f + K[i] + x[g], Sshift[i]) + b;
        a = temp;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
}

void
MD5Init(MD5_CTX *ctx)
{
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
    ctx->count[0] = 0;
    ctx->count[1] = 0;
}

void
MD5Update(MD5_CTX *ctx, const void *data, unsigned int len)
{
    const unsigned char *p = (const unsigned char *)data;

    /* bytes already buffered, derived from the bit count so far */
    unsigned int have = (unsigned int)((ctx->count[0] >> 3) & 0x3F);
    unsigned int need = 64 - have;

    uint32_t addbits = (uint32_t)len << 3;
    ctx->count[0] += addbits;
    if (ctx->count[0] < addbits) {
        ctx->count[1]++; /* carry */
    }
    ctx->count[1] += (uint32_t)((uint64_t)len >> 29);

    unsigned int off = 0;
    if (len >= need) {
        if (have) {
            memcpy(ctx->buffer + have, p, need);
            md5_compress(ctx, ctx->buffer);
            off = need;
            have = 0;
        }
        while (off + 64 <= len) {
            md5_compress(ctx, p + off);
            off += 64;
        }
    }
    if (off < len) {
        memcpy(ctx->buffer + have, p + off, len - off);
    }
}

void
MD5Final(unsigned char digest[MD5_RFC1321_DIGEST_LENGTH], MD5_CTX *ctx)
{
    uint32_t bits0 = ctx->count[0];
    uint32_t bits1 = ctx->count[1];

    unsigned int have = (unsigned int)((ctx->count[0] >> 3) & 0x3F);
    unsigned int padlen = (have < 56) ? (56 - have) : (120 - have);

    MD5Update(ctx, PADDING, padlen);

    unsigned char lenbuf[8];
    lenbuf[0] = (unsigned char)bits0; lenbuf[1] = (unsigned char)(bits0 >> 8);
    lenbuf[2] = (unsigned char)(bits0 >> 16); lenbuf[3] = (unsigned char)(bits0 >> 24);
    lenbuf[4] = (unsigned char)bits1; lenbuf[5] = (unsigned char)(bits1 >> 8);
    lenbuf[6] = (unsigned char)(bits1 >> 16); lenbuf[7] = (unsigned char)(bits1 >> 24);
    MD5Update(ctx, lenbuf, 8);

    for (int i = 0; i < 4; i++) {
        digest[i * 4] = (unsigned char)ctx->state[i];
        digest[i * 4 + 1] = (unsigned char)(ctx->state[i] >> 8);
        digest[i * 4 + 2] = (unsigned char)(ctx->state[i] >> 16);
        digest[i * 4 + 3] = (unsigned char)(ctx->state[i] >> 24);
    }
}
