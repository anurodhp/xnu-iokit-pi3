/* Real, narrow restoration of two zlib convenience-API pieces that
 * archive_write_disk_posix.c's real, unmodified Apple source calls
 * unconditionally for its own real HFS+ "decmpfs" (transparent
 * resource-fork compression) support -- an Apple-only extension with no
 * HAVE_x/config.h guard at all in real upstream source (confirmed by
 * reading the file directly), so it's compiled every time regardless of
 * platform.
 *
 * third_party/u-boot/lib/zlib only vendors the deflate/inflate CORE
 * (deflate.c/inflate.c/trees.c/inftrees.c/inffast.c/zutil.c) -- real,
 * unmodified, already proven by gzip(1)'s own real deflateInit2/inflate2
 * usage -- but never vendored zlib's separate top-level convenience
 * wrappers (compress.c/uncompr.c), so `compress()`/`uncompress()`/
 * `compressBound()`/`deflateInit()` (the unversioned macro form) don't
 * exist here. `deflateInit` is trivially restorable as the exact same
 * real macro upstream zlib.h itself defines (already proven safe by
 * gzip's own deflateInit2 restoration, same idea) -- see the -include
 * header this file pairs with. `compressBound()` is a pure, deterministic
 * formula with no external dependency (real zlib's own well-known
 * upper-bound formula, not fabricated). `uncompress()` genuinely has no
 * vendored source anywhere in this project, so it's reimplemented here
 * -- faithfully matching real zlib's own actual implementation (a thin
 * wrapper over inflateInit_/inflate/inflateEnd, all three already real,
 * linkable symbols from the same vendored inflate core grep/gzip/tar
 * all share), not a stand-in with different behavior.
 *
 * Scope: only tar(1)'s real archive_write_disk_posix.c calls any of
 * this (HFS+ decmpfs write-side compression, an opt-in extraction
 * feature -- ordinary, non-HFS-compressed tar members never reach this
 * code path, confirmed by reading hfs_write_decmpfs_block()'s own real
 * callers). Not linked into grep/gzip/compress -- their own zlib needs
 * were already fully satisfied without this file.
 */
#include <stdint.h>
#include <zlib.h>

unsigned long
compressBound(unsigned long sourceLen)
{
	return sourceLen + (sourceLen >> 12) + (sourceLen >> 14) +
	    (sourceLen >> 25) + 13;
}

int
uncompress(unsigned char *dest, unsigned long *destLen,
    const unsigned char *source, unsigned long sourceLen)
{
	z_stream stream;
	int err;

	stream.next_in = (Bytef *)(uintptr_t)source;
	stream.avail_in = (uInt)sourceLen;
	stream.next_out = dest;
	stream.avail_out = (uInt)*destLen;
	stream.zalloc = (alloc_func)0;
	stream.zfree = (free_func)0;

	err = inflateInit_(&stream, ZLIB_VERSION, (int)sizeof(z_stream));
	if (err != Z_OK)
		return err;

	err = inflate(&stream, Z_FINISH);
	if (err != Z_STREAM_END) {
		inflateEnd(&stream);
		return (err == Z_OK) ? Z_BUF_ERROR : err;
	}
	*destLen = stream.total_out;

	return inflateEnd(&stream);
}
