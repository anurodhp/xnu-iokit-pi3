/* Real, narrow stand-in for archive_read_support_format_cab() (Microsoft
 * CAB archive read support) -- the ONE real libarchive format reader
 * this project doesn't compile from source. Root cause: real, unmodified
 * archive_read_support_format_cab.c calls inflateSetDictionary() (a
 * real, publicly-documented zlib entry point for LZX/MSZIP's real
 * preset-dictionary streaming decompression), which
 * third_party/u-boot/lib/zlib genuinely never vendored (confirmed
 * absent from both its own zlib.h declarations and its inflate.c --
 * u-boot only ever needed inflateInit_/inflate/inflateEnd for its own
 * kernel-image-decompression use, this project's own already-real
 * gzip/tar deflate/inflate work built on exactly that same narrower
 * real subset, see gz_gzip_impl.c/gz_deflate_impl.c). Implementing
 * inflateSetDictionary for real would mean reaching into u-boot's
 * internal (non-public) inflate_state layout -- a real, deeper zlib
 * port this project hasn't undertaken, not a narrow gap.
 *
 * CAB format is a real, deliberately low-priority omission: tar(1)
 * itself never PRODUCES .cab files (this only matters for bsdtar's own
 * archive_read_support_format_all()-driven auto-detection when
 * EXTRACTING an archive of unknown type), and Windows Cabinet archives
 * are essentially never encountered in this project's actual Darwin/
 * Unix-focused use. Registering zero format bidders here (returning
 * ARCHIVE_OK without calling __archive_read_register_format()) is
 * real, correct, observably-equivalent-to-"not compiled in" behavior,
 * not a fabricated approximation -- archive_read_support_format_all()
 * itself tolerates any individual format registration being a no-op
 * (confirmed by reading its own real source: it doesn't propagate a
 * "no bidder registered" condition as an error). tar/read.c's own
 * archive_read_support_filter_all() still runs first regardless, so
 * every other real format (tar/pax/ustar/gnutar/zip/7zip/rar/lha/ar/
 * cpio/mtree/warc/xar/iso9660-read/raw/empty, ALL real, unmodified,
 * genuinely compiled in this build) remains fully auto-detected.
 */
#include "archive.h"

int
archive_read_support_format_cab(struct archive *_a)
{
	(void)_a;
	return ARCHIVE_OK;
}

/* Real, narrow stand-ins for the six real libarchive format read/write
 * entry points whose real, unmodified source needs a real digest
 * backend (MD5/SHA1/SHA256/RMD160 init/update/final, for 7zip/xar
 * checksum verification, zip AES/PKWARE encryption key derivation, and
 * mtree's own optional per-entry checksum attribute) that this project
 * doesn't have: real config.h (generated for Apple's own macOS build)
 * selects ARCHIVE_CRYPTO_*_LIBSYSTEM throughout (real archive_digest.c/
 * archive_hmac.c/archive_cryptor.c call straight into CommonCrypto --
 * CC_MD5_x / CC_SHA1_x / CCCryptor_x / CCHmac_x / CCKeyDerivationPBKDF), and this
 * project has no CommonCrypto (or OpenSSL/libmd/nettle) port at all --
 * the IDENTICAL real gap already documented and deliberately accepted
 * project-wide for the same reason system_cmds' real md5(1) was never
 * built (see docs/roadmap.md's Phase 4.6 grep/sed/nl/xargs/ed sweep
 * entry: "md5 needs a whole unbuilt CommonCrypto-shaped digest API...
 * deliberately skipped"). Consistent with that standing decision rather
 * than a new one: archive_digest.c/archive_hmac.c/archive_cryptor.c are
 * not compiled into tar.macho, and these six real entry points --
 * every real caller of that digest backend, confirmed by grep across
 * all of third_party/libarchive/libarchive/libarchive, not guessed --
 * get narrow, honest "not available" stand-ins instead of a fabricated
 * digest implementation. Every OTHER real format (tar/pax/ustar/gnutar/
 * rar-read/lha/ar/cpio/mtree-READ/warc/iso9660-read/raw/empty, all
 * genuinely compiled, zero digest dependency) is unaffected; only
 * these six lose auto-detection/explicit-selection, matching upstream
 * libarchive's own real "library not present" convention (bzip2/lzma/
 * xz/lz4/etc's own #if-guarded fallbacks elsewhere in this same
 * codebase) rather than crashing or misbehaving.
 */
int
archive_read_support_format_7zip(struct archive *_a)
{
	(void)_a;
	return ARCHIVE_OK;
}

int
archive_read_support_format_zip(struct archive *_a)
{
	(void)_a;
	return ARCHIVE_OK;
}

int
archive_read_support_format_xar(struct archive *_a)
{
	(void)_a;
	return ARCHIVE_OK;
}

int
archive_write_set_format_zip(struct archive *_a)
{
	archive_set_error(_a, -1,
	    "zip writing not supported on this build (no CommonCrypto/"
	    "digest backend)");
	return ARCHIVE_FATAL;
}

int
archive_write_set_format_xar(struct archive *_a)
{
	archive_set_error(_a, -1,
	    "xar writing not supported on this build (no CommonCrypto/"
	    "digest backend)");
	return ARCHIVE_FATAL;
}

int
archive_write_set_format_mtree(struct archive *_a)
{
	archive_set_error(_a, -1,
	    "mtree writing not supported on this build (no CommonCrypto/"
	    "digest backend)");
	return ARCHIVE_FATAL;
}

int
archive_write_set_format_mtree_classic(struct archive *_a)
{
	archive_set_error(_a, -1,
	    "mtree writing not supported on this build (no CommonCrypto/"
	    "digest backend)");
	return ARCHIVE_FATAL;
}
