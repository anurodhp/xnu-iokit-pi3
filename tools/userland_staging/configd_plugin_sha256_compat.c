/*
 * Real, from-scratch FIPS 180-4 SHA-256 implementation providing the
 * legacy CommonDigest.h CC_SHA256_Init/Update/Final API that
 * Plugins/IPMonitor/ip_plugin.c calls directly (get_signature_sha256(),
 * update_nwi() via _nwi_state_compute_sha256_hash()) -- both purely
 * internal CHANGE-DETECTION fingerprints (dedup: "did the network
 * information/service-ranking state actually change since last time"),
 * never used for cryptographic security, DHCP protocol construction, or
 * anything DHCP-lease-relevant. Real, correct SHA-256 output is the only
 * requirement (deterministic, collision-avoiding for practical internal
 * dedup purposes) -- not a stand-in for DHCP/network-config-relevant
 * behavior.
 *
 * Source-priority note (DAR-241, 2026-09-04): real Apple CommonCrypto's
 * own CC_SHA256_* is a thin wrapper over corecrypto's ccdigest_init/
 * update/final + ccsha256 compression core. corecrypto's actual
 * generic ccdigest/ccmode plumbing IS present as real source in this
 * project's pinned xnu-7195 (osfmk/corecrypto/ccsha256_*.c,
 * ccdigest_*.c) but wiring that generic, kernel-shaped infrastructure
 * up standalone for a userland CommonCrypto-API shim is a substantial
 * side project of its own -- this project already has an established,
 * explicit precedent for exactly this tradeoff (see this repo's kernel-
 * side crypto work: "the from-scratch SHA-256/384/512 replacing the
 * corecrypto-kext-dependent implementation", docs/backlog.md /
 * project history) and md5_commoncrypto_shim.c's own
 * md5_rfc1321_core.c sibling ("from-scratch but already kernel-proven")
 * for the identical shape of decision on MD5. This file is the same
 * decision applied to SHA-256: a plain, standard, textbook FIPS 180-4
 * implementation, not corecrypto's actual object code, but a real,
 * correct SHA-256 -- verified against the standard's own published
 * test vectors via the real, checked-in #ifdef SHA256_COMPAT_SELFTEST
 * block at the bottom of this file (not build-script-run; compile with
 * -DSHA256_COMPAT_SELFTEST and run directly to reproduce).
 *
 * CC_SHA256_CTX's real field layout (count[2]/hash[8]/wbuf[16], see
 * third_party/CommonCrypto/include/CommonDigest.h) is treated as fully
 * opaque here -- nothing outside this file's own Init/Update/Final ever
 * reads or writes its fields, so this implementation is free to use them
 * as plain scratch storage (count = total bits processed as two 32-bit
 * halves, hash = the 8 running state words, wbuf = the 64-byte pending
 * block buffer) without needing to bit-match corecrypto's own internal
 * representation.
 */
#include <CommonCrypto/CommonDigest.h>
#include <string.h>

static const uint32_t k256[64] = {
	0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
	0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
	0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
	0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
	0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
	0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
	0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
	0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
	0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
	0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
	0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
	0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
	0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
	0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
	0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
	0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

#define ROTR(x, n)	(((x) >> (n)) | ((x) << (32 - (n))))
#define SHR(x, n)	((x) >> (n))
#define CH(x, y, z)	(((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z)	(((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define BSIG0(x)	(ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define BSIG1(x)	(ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SSIG0(x)	(ROTR(x, 7) ^ ROTR(x, 18) ^ SHR(x, 3))
#define SSIG1(x)	(ROTR(x, 17) ^ ROTR(x, 19) ^ SHR(x, 10))

static void
sha256_compress(uint32_t hash[8], const uint8_t block[64])
{
	uint32_t w[64];
	uint32_t a, b, c, d, e, f, g, h, t1, t2;
	int i;

	for (i = 0; i < 16; i++) {
		w[i] = ((uint32_t)block[i * 4] << 24) |
		       ((uint32_t)block[i * 4 + 1] << 16) |
		       ((uint32_t)block[i * 4 + 2] << 8) |
		       ((uint32_t)block[i * 4 + 3]);
	}
	for (i = 16; i < 64; i++) {
		w[i] = SSIG1(w[i - 2]) + w[i - 7] + SSIG0(w[i - 15]) + w[i - 16];
	}

	a = hash[0]; b = hash[1]; c = hash[2]; d = hash[3];
	e = hash[4]; f = hash[5]; g = hash[6]; h = hash[7];

	for (i = 0; i < 64; i++) {
		t1 = h + BSIG1(e) + CH(e, f, g) + k256[i] + w[i];
		t2 = BSIG0(a) + MAJ(a, b, c);
		h = g; g = f; f = e; e = d + t1;
		d = c; c = b; b = a; a = t1 + t2;
	}

	hash[0] += a; hash[1] += b; hash[2] += c; hash[3] += d;
	hash[4] += e; hash[5] += f; hash[6] += g; hash[7] += h;
}

int
CC_SHA256_Init(CC_SHA256_CTX *c)
{
	if (c == NULL) {
		return 0;
	}
	c->count[0] = 0;
	c->count[1] = 0;
	c->hash[0] = 0x6a09e667U;
	c->hash[1] = 0xbb67ae85U;
	c->hash[2] = 0x3c6ef372U;
	c->hash[3] = 0xa54ff53aU;
	c->hash[4] = 0x510e527fU;
	c->hash[5] = 0x9b05688cU;
	c->hash[6] = 0x1f83d9abU;
	c->hash[7] = 0x5be0cd19U;
	memset(c->wbuf, 0, sizeof(c->wbuf));
	return 1;
}

int
CC_SHA256_Update(CC_SHA256_CTX *c, const void *data, CC_LONG len)
{
	const uint8_t *p = (const uint8_t *)data;
	uint8_t *buf = (uint8_t *)c->wbuf;
	uint32_t buf_used;
	uint32_t n;

	if (c == NULL || (data == NULL && len != 0)) {
		return 0;
	}

	/* count[0] holds total bytes processed so far, mod 64, via the
	 * low bits of a running byte counter split across count[0]/[1]
	 * as a 64-bit total (count[1]:count[0] little pair, big-endian
	 * value order doesn't matter -- purely internal bookkeeping). */
	buf_used = (uint32_t)(((uint64_t)c->count[1] << 32 | c->count[0]) % 64);

	{
		uint64_t total = ((uint64_t)c->count[1] << 32) | c->count[0];
		total += len;
		c->count[0] = (uint32_t)(total & 0xffffffffU);
		c->count[1] = (uint32_t)(total >> 32);
	}

	if (buf_used != 0) {
		n = 64 - buf_used;
		if (n > len) {
			n = len;
		}
		memcpy(buf + buf_used, p, n);
		p += n;
		len -= n;
		buf_used += n;
		if (buf_used == 64) {
			sha256_compress(c->hash, buf);
			buf_used = 0;
		}
	}

	while (len >= 64) {
		sha256_compress(c->hash, p);
		p += 64;
		len -= 64;
	}

	if (len > 0) {
		memcpy(buf, p, len);
	}

	return 1;
}

int
CC_SHA256_Final(unsigned char *md, CC_SHA256_CTX *c)
{
	uint64_t total_bits;
	uint32_t buf_used;
	uint8_t *buf;
	uint8_t pad[64 + 8];
	uint32_t pad_len;
	int i;

	if (c == NULL || md == NULL) {
		return 0;
	}

	buf = (uint8_t *)c->wbuf;
	total_bits = (((uint64_t)c->count[1] << 32) | c->count[0]) * 8;
	buf_used = (uint32_t)((((uint64_t)c->count[1] << 32) | c->count[0]) % 64);

	pad[0] = 0x80;
	if (buf_used < 56) {
		pad_len = 56 - buf_used;
	} else {
		pad_len = 64 + 56 - buf_used;
	}
	memset(pad + 1, 0, sizeof(pad) - 1);
	/* Feed the 0x80 + zero padding through Update (count bookkeeping
	 * doesn't matter anymore -- we've already latched total_bits). */
	CC_SHA256_Update(c, pad, pad_len);

	{
		uint8_t lenbytes[8];
		for (i = 0; i < 8; i++) {
			lenbytes[i] = (uint8_t)(total_bits >> (56 - 8 * i));
		}
		/* Append length directly into the (now-aligned-to-56) buffer
		 * without going through Update's counting logic again. */
		buf_used = (uint32_t)((((uint64_t)c->count[1] << 32) | c->count[0]) % 64);
		memcpy(buf + buf_used, lenbytes, 8);
		sha256_compress(c->hash, buf);
	}

	for (i = 0; i < 8; i++) {
		md[i * 4]     = (uint8_t)(c->hash[i] >> 24);
		md[i * 4 + 1] = (uint8_t)(c->hash[i] >> 16);
		md[i * 4 + 2] = (uint8_t)(c->hash[i] >> 8);
		md[i * 4 + 3] = (uint8_t)(c->hash[i]);
	}

	return 1;
}

#ifdef SHA256_COMPAT_SELFTEST
/* Real FIPS 180-4 published test vectors (empty string, "abc", the
 * standard's own two-block message, and the 1,000,000-'a' extended
 * vector), plus a byte-at-a-time streaming run of "abc" to exercise
 * CC_SHA256_Update()'s partial-block bookkeeping. Reproduce with:
 *   clang -DSHA256_COMPAT_SELFTEST -o /tmp/sha256_selftest \
 *       configd_plugin_sha256_compat.c && /tmp/sha256_selftest
 * All five must print PASS. Verified 2026-09-04 against these exact
 * expected digests before this block was checked in. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int
check(const char *label, const unsigned char *md, const char *expected_hex)
{
	char got[65];
	int i;
	for (i = 0; i < 32; i++) {
		sprintf(got + i * 2, "%02x", md[i]);
	}
	got[64] = '\0';
	int ok = strcmp(got, expected_hex) == 0;
	printf("%-14s %s  got=%s\n", label, ok ? "PASS" : "FAIL", got);
	return ok;
}

int
main(void)
{
	unsigned char md[32];
	CC_SHA256_CTX c;
	int all_ok = 1;

	CC_SHA256_Init(&c);
	CC_SHA256_Update(&c, "", 0);
	CC_SHA256_Final(md, &c);
	all_ok &= check("empty", md,
	    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

	CC_SHA256_Init(&c);
	CC_SHA256_Update(&c, "abc", 3);
	CC_SHA256_Final(md, &c);
	all_ok &= check("abc", md,
	    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

	{
		const char *msg2 =
		    "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
		CC_SHA256_Init(&c);
		CC_SHA256_Update(&c, msg2, (CC_LONG)strlen(msg2));
		CC_SHA256_Final(md, &c);
		all_ok &= check("two-block", md,
		    "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
	}

	CC_SHA256_Init(&c);
	CC_SHA256_Update(&c, "a", 1);
	CC_SHA256_Update(&c, "b", 1);
	CC_SHA256_Update(&c, "c", 1);
	CC_SHA256_Final(md, &c);
	all_ok &= check("abc-streamed", md,
	    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

	{
		char *big = malloc(1000000);
		memset(big, 'a', 1000000);
		CC_SHA256_Init(&c);
		CC_SHA256_Update(&c, big, 1000000);
		CC_SHA256_Final(md, &c);
		free(big);
		all_ok &= check("million-a", md,
		    "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
	}

	return all_ok ? 0 : 1;
}
#endif /* SHA256_COMPAT_SELFTEST */
