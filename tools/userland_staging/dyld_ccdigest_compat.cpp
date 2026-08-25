/* Real, standard SHA-1/256/384 implementation behind the real, public
 * corecrypto `ccdigest_info`/`ccdigest_init`/`ccdigest_update`/
 * `ccdigest_final` API (third_party/xnu-7195/EXTERNAL_HEADERS/corecrypto/
 * ccdigest.h -- already vendored into this project's existing xnu-7195
 * pin, used as-is here, not newly introduced). Needed by real
 * dyld3/MachOLoaded.cpp's forEachCDHashOfCodeSignature() (computes the
 * cdHash of a Mach-O's embedded code signature) -- the only real,
 * functional (non-dead-include) corecrypto consumer found while porting
 * dyld (see build_dyld_core.sh's header comment for glue.c's separate,
 * genuinely dead corecrypto includes).
 *
 * .cpp, not .c: this vendored EXTERNAL_HEADERS excerpt of ccdigest.h/
 * ccsha1.h/ccsha2.h has no `extern "C"` guard at all (a real, confirmed
 * incompleteness of pulling a kernel-oriented header excerpt for a
 * userland C++ consumer -- xnu's own C build never needed one). Real
 * dyld's actual macOS SDK copy of these headers does have one, so a
 * real C++ consumer like MachOLoaded.cpp genuinely expects C++-mangled
 * names for ccsha1_di()/ccsha256_di()/ccsha384_di(). Compiling this
 * file as C++ against the SAME unguarded declarations makes both sides
 * apply the same (C++) mangling, guaranteeing they agree -- simpler and
 * more robust than hand-patching extern "C" onto a vendored header.
 *
 * Real corecrypto's actual .c implementations of these same functions
 * (third_party/xnu-7195/osfmk/corecrypto/ccdigest_init.c etc) are real
 * Apple source but need internal-only headers (corecrypto/cc_priv.h,
 * ccdigest_priv.h, cc_runtime_config.h, fipspost_trace.h, and -- for
 * cc_abort.c specifically -- an unrelated <RTK_platform.h>) that exist
 * nowhere in any apple-oss-distributions release, confirmed by
 * attempting the real compile first, not guessed -- the full corecrypto
 * internal implementation was never open-sourced (matches this
 * project's already-documented corecrypto gap, see project memory
 * project_xnu_rpi3_crypto_gap.md). Real SHA256/384 algorithm logic below
 * is ported directly from this project's OWN already-real,
 * already-QEMU-verified from-scratch kernel implementation
 * (third_party/xnu-7195/libkern/crypto/corecrypto_sha2.c, written for
 * the exact same "corecrypto not available" reason) -- re-expressed
 * against corecrypto's real, documented `ccdigest_info`/compress/final
 * contract instead of that file's own SHA256_CTX-style API, so the two
 * are independent code but the same trusted, standard, FIPS 180-4
 * algorithm. SHA-1 is added fresh here (not needed kernel-side), same
 * standard algorithm (FIPS 180-4 / RFC 3174), nothing exotic.
 *
 * Real corecrypto ccdigest_ctx layout (from ccdigest.h's own documented
 * macros, not guessed): [nbits:uint64_t][state:state_size bytes]
 * [pending data:block_size bytes][num:uint32_t]. ccdigest_init/update
 * below are written directly against those real macros
 * (ccdigest_nbits/ccdigest_state_u8/ccdigest_data/ccdigest_num), so they
 * work correctly for any real ccdigest_info, not just the three defined
 * here.
 */

#include <corecrypto/ccdigest.h>
#include <corecrypto/ccsha1.h>
#include <corecrypto/ccsha2.h>
#include <string.h>

/* ---- shared big-endian helpers ---- */

static inline uint32_t load_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static inline void store_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static inline uint64_t load_be64(const uint8_t *p) {
    return ((uint64_t)load_be32(p) << 32) | (uint64_t)load_be32(p + 4);
}
static inline void store_be64(uint8_t *p, uint64_t v) {
    store_be32(p, (uint32_t)(v >> 32)); store_be32(p + 4, (uint32_t)v);
}
static inline uint32_t rotl32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
static inline uint32_t rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
static inline uint64_t rotr64(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }

/* ================= generic real ccdigest_info driver ================= */
/* Real, standard init/update against ccdigest.h's own documented ctx
 * layout -- shared by every ccdigest_info below. */

void ccdigest_init(const struct ccdigest_info *di, ccdigest_ctx_t ctx) {
    memcpy(ccdigest_state_u8(di, ctx), di->initial_state, di->state_size);
    ccdigest_nbits(di, ctx) = 0;
    ccdigest_num(di, ctx) = 0;
}

void ccdigest_update(const struct ccdigest_info *di, ccdigest_ctx_t ctx, size_t len, const void *data) {
    const uint8_t *p = (const uint8_t *)data;
    unsigned char *buf = ccdigest_data(di, ctx);
    unsigned int *num = &ccdigest_num(di, ctx);

    ccdigest_nbits(di, ctx) += (uint64_t)len * 8;

    if (*num > 0) {
        size_t take = di->block_size - *num;
        if (take > len) take = len;
        memcpy(buf + *num, p, take);
        *num += (unsigned int)take;
        p += take;
        len -= take;
        if (*num == di->block_size) {
            di->compress(ccdigest_state(di, ctx), 1, buf);
            *num = 0;
        }
    }
    if (len >= di->block_size) {
        size_t nblocks = len / di->block_size;
        di->compress(ccdigest_state(di, ctx), nblocks, p);
        p += nblocks * di->block_size;
        len -= nblocks * di->block_size;
    }
    if (len > 0) {
        memcpy(buf, p, len);
        *num = (unsigned int)len;
    }
}

/* Real, standard Merkle-Damgard final padding (0x80, zero pad, big-
 * endian bit-length in the trailing length field) -- shared shape
 * between SHA-1/256 (block_size=64, 8-byte length) and SHA-384/512
 * (block_size=128, 16-byte length field, top 8 bytes always zero here
 * since no real Mach-O code signature is anywhere near 2^64 bytes). */
static void pad_and_compress(const struct ccdigest_info *di, ccdigest_ctx_t ctx, size_t length_field_size) {
    uint64_t bit_len = ccdigest_nbits(di, ctx);
    unsigned char *buf = ccdigest_data(di, ctx);
    unsigned int num = ccdigest_num(di, ctx);

    size_t footer = length_field_size;
    buf[num++] = 0x80;
    if (num > di->block_size - footer) {
        memset(buf + num, 0, di->block_size - num);
        di->compress(ccdigest_state(di, ctx), 1, buf);
        num = 0;
    }
    memset(buf + num, 0, di->block_size - footer - num);
    if (footer == 16) {
        memset(buf + di->block_size - 16, 0, 8);
        store_be64(buf + di->block_size - 8, bit_len);
    } else {
        store_be64(buf + di->block_size - 8, bit_len);
    }
    di->compress(ccdigest_state(di, ctx), 1, buf);
}

/* ccdigest_final() itself is already a real CC_INLINE definition in
 * ccdigest.h (just calls di->final()) -- not redefined here. */

/* ================= SHA-1 ================= */

static const uint32_t sha1_initial_state[5] = {
    0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0,
};

static void sha1_compress(ccdigest_state_t state, size_t nblocks, const void *data) {
    uint32_t *h = ccdigest_u32(state);
    const uint8_t *block = (const uint8_t *)data;
    for (size_t b = 0; b < nblocks; b++, block += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) w[i] = load_be32(block + i * 4);
        for (int i = 16; i < 80; i++) w[i] = rotl32(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

        uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20)      { f = (bb & c) | (~bb & d);         k = 0x5A827999; }
            else if (i < 40) { f = bb ^ c ^ d;                   k = 0x6ED9EBA1; }
            else if (i < 60) { f = (bb & c) | (bb & d) | (c & d); k = 0x8F1BBCDC; }
            else             { f = bb ^ c ^ d;                   k = 0xCA62C1D6; }
            uint32_t temp = rotl32(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rotl32(bb, 30); bb = a; a = temp;
        }
        h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e;
    }
}

static void sha1_final(const struct ccdigest_info *di, ccdigest_ctx_t ctx, unsigned char *digest) {
    pad_and_compress(di, ctx, 8);
    uint32_t *h = ccdigest_state_u32(di, ctx);
    for (int i = 0; i < 5; i++) store_be32(digest + i * 4, h[i]);
}

const struct ccdigest_info ccsha1_di_inst = {
    .output_size = 20, .state_size = 20, .block_size = 64,
    .oid_size = 0, .oid = NULL,
    .initial_state = sha1_initial_state,
    .compress = sha1_compress, .final = sha1_final,
};
const struct ccdigest_info *ccsha1_di(void) { return &ccsha1_di_inst; }

/* ================= SHA-256 ================= */

static const uint32_t sha256_initial_state[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};
static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static void sha256_compress(ccdigest_state_t state, size_t nblocks, const void *data) {
    uint32_t *h = ccdigest_u32(state);
    const uint8_t *block = (const uint8_t *)data;
    for (size_t b = 0; b < nblocks; b++, block += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++) w[i] = load_be32(block + i * 4);
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = rotr32(w[i-15], 7) ^ rotr32(w[i-15], 18) ^ (w[i-15] >> 3);
            uint32_t s1 = rotr32(w[i-2], 17) ^ rotr32(w[i-2], 19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t temp1 = hh + S1 + ch + sha256_k[i] + w[i];
            uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
            uint32_t maj = (a & bb) ^ (a & c) ^ (bb & c);
            uint32_t temp2 = S0 + maj;
            hh = g; g = f; f = e; e = d + temp1;
            d = c; c = bb; bb = a; a = temp1 + temp2;
        }
        h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
}

static void sha256_final(const struct ccdigest_info *di, ccdigest_ctx_t ctx, unsigned char *digest) {
    pad_and_compress(di, ctx, 8);
    uint32_t *h = ccdigest_state_u32(di, ctx);
    for (int i = 0; i < 8; i++) store_be32(digest + i * 4, h[i]);
}

const struct ccdigest_info ccsha256_di_inst = {
    .output_size = 32, .state_size = 32, .block_size = 64,
    .oid_size = 0, .oid = NULL,
    .initial_state = sha256_initial_state,
    .compress = sha256_compress, .final = sha256_final,
};
const struct ccdigest_info *ccsha256_di(void) { return &ccsha256_di_inst; }

/* ================= SHA-384 (SHA-512 core, truncated output) ================= */

static const uint64_t sha384_initial_state[8] = {
    0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL, 0x9159015a3070dd17ULL, 0x152fecd8f70e5939ULL,
    0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL, 0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL,
};
static const uint64_t sha512_k[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};

static void sha512_compress(ccdigest_state_t state, size_t nblocks, const void *data) {
    uint64_t *h = ccdigest_u64(state);
    const uint8_t *block = (const uint8_t *)data;
    for (size_t b = 0; b < nblocks; b++, block += 128) {
        uint64_t w[80];
        for (int i = 0; i < 16; i++) w[i] = load_be64(block + i * 8);
        for (int i = 16; i < 80; i++) {
            uint64_t s0 = rotr64(w[i-15], 1) ^ rotr64(w[i-15], 8) ^ (w[i-15] >> 7);
            uint64_t s1 = rotr64(w[i-2], 19) ^ rotr64(w[i-2], 61) ^ (w[i-2] >> 6);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint64_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 80; i++) {
            uint64_t S1 = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41);
            uint64_t ch = (e & f) ^ (~e & g);
            uint64_t temp1 = hh + S1 + ch + sha512_k[i] + w[i];
            uint64_t S0 = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39);
            uint64_t maj = (a & bb) ^ (a & c) ^ (bb & c);
            uint64_t temp2 = S0 + maj;
            hh = g; g = f; f = e; e = d + temp1;
            d = c; c = bb; bb = a; a = temp1 + temp2;
        }
        h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
}

static void sha384_final(const struct ccdigest_info *di, ccdigest_ctx_t ctx, unsigned char *digest) {
    pad_and_compress(di, ctx, 16);
    uint64_t *h = ccdigest_state_u64(di, ctx);
    uint8_t full[64];
    for (int i = 0; i < 8; i++) store_be64(full + i * 8, h[i]);
    memcpy(digest, full, 48); /* SHA-384 = first 384 bits of the SHA-512-shaped state */
}

const struct ccdigest_info ccsha384_di_inst = {
    .output_size = 48, .state_size = 64, .block_size = 128,
    .oid_size = 0, .oid = NULL,
    .initial_state = sha384_initial_state,
    .compress = sha512_compress, .final = sha384_final,
};
const struct ccdigest_info *ccsha384_di(void) { return &ccsha384_di_inst; }
