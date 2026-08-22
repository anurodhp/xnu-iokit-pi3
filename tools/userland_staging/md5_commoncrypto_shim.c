/*
 * md5_commoncrypto_shim.c - thin CommonCrypto-shaped digest API shim
 * (docs/roadmap.md Phase 4.6 item 6), letting real, UNMODIFIED
 * third_party/text_cmds/md5/md5.c (tag text_cmds-106) link and run on
 * this project's target without any source changes to md5.c itself.
 *
 * md5.c's own real, unmodified sibling in the same directory,
 * commoncrypto.h, declares exactly three functions this file implements
 * (Digest_End/Digest_Data/Digest_File) after `#include`ing real Apple
 * <CommonCrypto/CommonDigest.h> and <CommonCrypto/CommonDigestSPI.h> --
 * both vendored verbatim from third_party/CommonCrypto (tag
 * CommonCrypto-60178.40.2, confirmed against the macos-112 manifest the
 * same way every other pin in this project is, see
 * setup_third_party.sh's own header comment) into
 * $OUT/commoncrypto_glue/CommonCrypto/ by build_simple_cmds.sh at build
 * time. Those two real headers are the actual source of CCDigestCtx's
 * opaque `uint8_t context[CC_DIGEST_SIZE]` layout, CCDigestAlgorithm's
 * kCCDigestMD5/etc enumerators, and every prototype this file defines
 * (CCDigestInit/CCDigestUpdate/CCDigestFinal/CCDigestOutputSize) --
 * their signatures are real, not invented.
 *
 * What ISN'T real-Apple-source here, and why (see
 * md5_rfc1321_core.c's own header for the full source-priority
 * investigation): real CommonCrypto's own lib/CommonDigest.c is a thin
 * wrapper over corecrypto's ccdigest_init/update/final, and corecrypto's
 * actual digest cores are not open-sourced anywhere Apple ships (verified
 * absent from this checkout, not assumed) -- so CCDigestInit/Update/
 * Final below dispatch to md5_rfc1321_core.c's own from-scratch (but
 * already kernel-proven, see that file's header) RFC 1321 MD5 instead.
 * Only kCCDigestMD5 is actually implemented; kCCDigestSHA1/SHA256/
 * RMD160 (referenced only as compile-time enum constants in md5.c's own
 * `Algorithm[]` table, for the sha1(1)/sha256(1)/rmd160(1) argv[0]-
 * dispatch aliases this project does NOT build/install) return
 * kCCUnimplemented-equivalent failure if ever actually invoked -- honest
 * about the real, current scope, not a fabricated success. Real
 * `CC_MD5_Init`/`CC_MD5_CTX`-family symbols from CommonDigest.h are
 * deliberately NOT defined here: md5.c only ever calls through the
 * CCDigest*() SPI layer (via commoncrypto.h's Digest_* wrappers), never
 * the plain CC_MD5_* family directly, so those symbols are declared
 * (real header) but never referenced -- nothing to implement or fake.
 *
 * Digest_File() here is a plain blocking open()/read()-loop
 * reimplementation of real commoncrypto.c's Digest_File(), which is
 * itself built on dispatch_io()/dispatch_semaphore_t/GCD queues --
 * this project's own tools/userland_staging/README.md already
 * documents libdispatch as investigated and ruled out project-wide
 * ("a real monolith, not separable file-by-file the way every prior
 * tier was"), so the real file can't be linked unmodified here. Same
 * observable result (digest of the whole file, real open()/read()
 * error propagation via errno), different mechanism.
 */

#include <CommonCrypto/CommonDigest.h>
#include <CommonCrypto/CommonDigestSPI.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "md5_rfc1321_core.h"

typedef struct {
    CCDigestAlgorithm alg;
    MD5_CTX md5;
} shim_digest_ctx_t;

_Static_assert(sizeof(shim_digest_ctx_t) <= sizeof(CCDigestCtx),
    "shim_digest_ctx_t no longer fits inside the real opaque CCDigestCtx");

int
CCDigestInit(CCDigestAlgorithm algorithm, CCDigestRef ctx)
{
    shim_digest_ctx_t *s = (shim_digest_ctx_t *)ctx;

    if (ctx == NULL) {
        return -1;
    }
    memset(s, 0, sizeof(*s));
    s->alg = algorithm;

    switch (algorithm) {
    case kCCDigestMD5:
        MD5Init(&s->md5);
        return 0;
    default:
        /* Not implemented -- see this file's own header comment. */
        return -1;
    }
}

int
CCDigestUpdate(CCDigestRef ctx, const void *data, size_t length)
{
    shim_digest_ctx_t *s = (shim_digest_ctx_t *)ctx;

    if (ctx == NULL) {
        return -1;
    }
    if (length == 0) {
        return 0;
    }
    if (data == NULL) {
        return -1;
    }

    switch (s->alg) {
    case kCCDigestMD5:
        MD5Update(&s->md5, data, (unsigned int)length);
        return 0;
    default:
        return -1;
    }
}

int
CCDigestFinal(CCDigestRef ctx, uint8_t *output)
{
    shim_digest_ctx_t *s = (shim_digest_ctx_t *)ctx;

    if (ctx == NULL || output == NULL) {
        return -1;
    }

    switch (s->alg) {
    case kCCDigestMD5:
        MD5Final(output, &s->md5);
        return 0;
    default:
        return -1;
    }
}

size_t
CCDigestOutputSize(CCDigestRef ctx)
{
    shim_digest_ctx_t *s = (shim_digest_ctx_t *)ctx;

    if (ctx == NULL) {
        return 0;
    }
    switch (s->alg) {
    case kCCDigestMD5:
        return CC_MD5_DIGEST_LENGTH;
    default:
        return 0;
    }
}

/*
 * CC_MD5() -- the legacy one-shot CommonDigest.h wrapper (distinct from
 * the CCDigest*() SPI family above). Real Apple CommonDigest.c
 * implements this the same way, in terms of CC_MD5_Init/Update/Final
 * (themselves thin wrappers over corecrypto, same absent-from-this-
 * checkout situation the CCDigest*() functions above already document)
 * -- dispatches to the same real, kernel-proven md5_rfc1321_core.c this
 * whole file already uses, not a separate implementation. First real
 * caller: third_party/cron/crontab/crontab.c's MD5File() (Phase 5.5,
 * cron port, 2026-08-22), a new consumer beyond md5.c's original scope
 * -- CC_MD5_Init/_Update/_Final themselves remain declared-but-unused
 * (real header, no caller needs the multi-step form here either).
 */
unsigned char *
CC_MD5(const void *data, CC_LONG len, unsigned char *md)
{
    MD5_CTX ctx;

    MD5Init(&ctx);
    if (len != 0) {
        MD5Update(&ctx, data, (unsigned int)len);
    }
    MD5Final(md, &ctx);
    return md;
}

/*
 * Real text_cmds/md5/commoncrypto.h's own declared surface -- see this
 * file's header comment for how Digest_File() differs from real
 * commoncrypto.c's dispatch_io()-based version.
 */

char *
Digest_End(CCDigestRef ctx, char *buf)
{
    static const char hex[] = "0123456789abcdef";
    uint8_t digest[64]; /* SHA512 would be the biggest if ever wired up */
    size_t length, i;

    length = CCDigestOutputSize(ctx);
    if (length == 0 || length > sizeof(digest)) {
        return NULL;
    }
    if (CCDigestFinal(ctx, digest) != 0) {
        return NULL;
    }
    for (i = 0; i < length; i++) {
        buf[i + i] = hex[digest[i] >> 4];
        buf[i + i + 1] = hex[digest[i] & 0x0f];
    }
    buf[i + i] = '\0';
    return buf;
}

char *
Digest_Data(CCDigestAlg algorithm, const void *data, size_t len, char *buf)
{
    CCDigestCtx ctx;

    if (CCDigestInit(algorithm, &ctx) != 0) {
        return NULL;
    }
    if (len != 0 && CCDigestUpdate(&ctx, data, len) != 0) {
        return NULL;
    }
    return Digest_End(&ctx, buf);
}

char *
Digest_File(CCDigestAlg algorithm, const char *filename, char *buf)
{
    CCDigestCtx ctx;
    int fd;
    ssize_t n;
    unsigned char chunk[8192];

    if (CCDigestInit(algorithm, &ctx) != 0) {
        return NULL;
    }

    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        return NULL;
    }

    while ((n = read(fd, chunk, sizeof(chunk))) > 0) {
        if (CCDigestUpdate(&ctx, chunk, (size_t)n) != 0) {
            int e = errno;
            close(fd);
            errno = e ? e : EIO;
            return NULL;
        }
    }
    if (n < 0) {
        int e = errno;
        close(fd);
        errno = e;
        return NULL;
    }
    close(fd);

    return Digest_End(&ctx, buf);
}
