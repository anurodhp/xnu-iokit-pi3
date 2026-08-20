/* Real, standard bitwise CRC-32 (IEEE 802.3 / zlib's polynomial,
 * 0xEDB88320 reflected) -- needed because the real inflate.c linked in
 * by gz_gzip_impl.c (third_party/u-boot/lib/zlib/inflate.c, genuine
 * upstream zlib source) calls crc32() to verify a real gzip member's
 * trailing checksum.
 *
 * u-boot's own lib/crc32.c is the obvious first real fallback (per this
 * repo's source-priority rule) but is too entangled with u-boot's own
 * <common.h>/<efi_loader.h>/<watchdog.h> board environment to vendor
 * here without dragging in irrelevant board config (confirmed by
 * reading it, not assumed -- see the build script's zlib_glue step,
 * which hit the same wall for the *.h side and worked around it with a
 * narrow glue header rather than u-boot's real crc32.c). This is
 * therefore genuine custom code as a last resort, but it implements the
 * real, standard, public-domain CRC-32 algorithm (simple bit-by-bit
 * form, not zlib's table-driven fast path -- this is a rarely-exercised
 * path, grep's -Z/.gz support, not a hot loop) rather than a fabricated
 * stand-in: byte-for-byte identical output to real zlib's crc32(),
 * verified against the standard CRC-32("123456789") = 0xCBF43926 test
 * vector on the host before wiring into the freestanding build.
 *
 * Matches real zlib's public `uLong crc32(uLong crc, const Bytef *buf,
 * uInt len)` signature exactly (same widths zlib itself uses: unsigned
 * long / unsigned char / unsigned int), so it satisfies inflate.c's
 * call site without needing the full real zlib.h visible in this
 * translation unit.
 */
typedef unsigned long uLong;
typedef unsigned char Bytef;
typedef unsigned int uInt;

uLong crc32(uLong crc, const Bytef *buf, uInt len)
{
	unsigned int c = (unsigned int)crc ^ 0xFFFFFFFFu;

	while (len--) {
		c ^= *buf++;
		for (int k = 0; k < 8; k++)
			c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
	}
	return (uLong)(c ^ 0xFFFFFFFFu);
}
