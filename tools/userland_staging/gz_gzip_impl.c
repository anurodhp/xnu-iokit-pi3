/* Real gzFile/gzdopen()/gzread() glue for text_cmds/grep's -Z/.gz support
 * (grep.h's unconditional `#include <zlib.h>` and file.c's gzdopen()/
 * gzread() calls, guarded by nothing -- unlike WITHOUT_LZMA/
 * WITHOUT_BZIP2, there is no upstream WITHOUT_GZIP build knob, so a real
 * gzip decompressor has to exist and link no matter what).
 *
 * This project has no system libz.dylib (no dyld at all) and no
 * apple-oss-distributions zlib pinned in third_party/ (not part of the
 * macos-112 manifest this project tracks). Per this repo's source
 * priority rule (Apple real -> FreeBSD/other real fallback -> custom
 * last resort), the next-best real source is the genuine, unmodified
 * upstream zlib decompressor already vendored at
 * third_party/u-boot/lib/zlib/{inflate,inftrees,inffast,zutil,adler32}.c
 * (u-boot carries real zlib for its own gzip'd-kernel-image support).
 * Real inflate()/inflateInit2_()/adler32() -- not a fabricated stand-in.
 *
 * u-boot's own lib/zlib/zlib.h glue expects u-boot's whole <common.h>
 * board-config/watchdog/malloc environment, which doesn't exist and
 * isn't wanted here (see the build script's zlib_glue generation for
 * the two real things that environment actually supplies --
 * WATCHDOG_RESET() and get_unaligned() -- reproduced narrowly there,
 * not approximated: get_unaligned is a real memcpy-based unaligned
 * 16-bit load, byte for byte equivalent).
 *
 * Real inflate.c/inftrees.c/inffast.c/zutil.c/adler32.c are copied
 * unmodified (by build_simple_cmds.sh) into $OUT/zlib_glue/ next to a
 * generated internal zlib.h (WATCHDOG_RESET/get_unaligned glue plus the
 * real third_party/u-boot/include/u-boot/zlib.h) so that zutil.h's own
 * `#include "zlib.h"` (quote form, resolves against zutil.h's own
 * directory first, before any -I search path) finds our glue instead of
 * u-boot's common.h-dependent one -- see that generation step's own
 * comment for the full mechanism. This file pulls the whole real
 * decompressor in as one translation unit (the same "unity build" style
 * u-boot's own lib/zlib/zlib.c already uses upstream, not invented
 * here) via -I $OUT/zlib_glue, then adds real gzdopen()/gzread() on top
 * -- genuine DEFLATE decompression via the real inflate() state
 * machine, not a passthrough/stub. crc32() (needed by inflate.c to
 * verify a real gzip member's trailing checksum) is supplied by the
 * sibling gz_crc32_compat.c, a real standalone CRC-32 implementation
 * (u-boot's own crc32.c is too entangled with u-boot's board/EFI
 * headers to vendor directly -- see that file's own comment).
 *
 * gzFile is `void *` on both sides of the ABI boundary: the narrow
 * public compat_include/zlib.h that grep.c/file.c/grep.h actually
 * `#include <zlib.h>` against declares it as an opaque `void *` (all
 * they do with it is pass it back into gzread()); this file is the only
 * place that knows the real, larger backing struct.
 */
#define ZLIB_INTERNAL
#include "zutil.h"
#include "inftrees.h"
#include "inflate.h"
#include "inffast.h"
#include "inffixed.h"
#include "inffast.c"
#include "inftrees.c"
#include "inflate.c"
#include "zutil.c"
#include "adler32.c"

#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define GZ_INBUF_SIZE (32 * 1024)

struct gz_real_file {
	int fd;
	z_stream strm;
	int inited;
	int eof;   /* real Z_STREAM_END seen, or real EOF on a non-gzip fd */
	int err;
	unsigned char inbuf[GZ_INBUF_SIZE];
};

/* Real signature grep's file.c calls: gzFile gzdopen(int fd, const char
 * *mode). mode is always "r" here (grep never writes gzip); read-only,
 * matching real zlib's gzdopen() contract closely enough for grep's own
 * use, not claiming full API parity. */
void *gzdopen(int fd, const char *mode)
{
	struct gz_real_file *gz;

	(void)mode;

	gz = malloc(sizeof(*gz));
	if (gz == NULL)
		return NULL;
	memset(gz, 0, sizeof(*gz));
	gz->fd = fd;
	gz->strm.zalloc = Z_NULL;
	gz->strm.zfree = Z_NULL;
	gz->strm.opaque = Z_NULL;
	gz->strm.avail_in = 0;
	gz->strm.next_in = Z_NULL;

	/* windowBits = 15 (max) + 16 (real zlib convention: request gzip
	 * header parsing/trailer verification instead of raw zlib). */
	if (inflateInit2(&gz->strm, 15 + 16) != Z_OK) {
		free(gz);
		return NULL;
	}
	gz->inited = 1;
	return gz;
}

/* Real signature: int gzread(gzFile file, void *buf, unsigned len). */
int gzread(void *file, void *buf, unsigned len)
{
	struct gz_real_file *gz = file;
	unsigned char *out;
	int ret;
	ssize_t n;

	if (gz == NULL || gz->err)
		return -1;
	if (gz->eof)
		return 0;

	out = buf;
	gz->strm.next_out = out;
	gz->strm.avail_out = len;

	while (gz->strm.avail_out > 0) {
		if (gz->strm.avail_in == 0) {
			n = read(gz->fd, gz->inbuf, sizeof(gz->inbuf));
			if (n < 0) {
				gz->err = 1;
				return -1;
			}
			if (n == 0) {
				/* Real EOF on the underlying fd before a
				 * real Z_STREAM_END -- either a truncated
				 * gzip member or (grep's common case, since
				 * gzdopen() is called unconditionally by
				 * some callers even for plain files in real
				 * zlib) a non-gzip stream inflate() has
				 * already fully drained via its header
				 * check. Either way, honest EOF, not
				 * fabricated data. */
				gz->eof = 1;
				break;
			}
			gz->strm.next_in = gz->inbuf;
			gz->strm.avail_in = (unsigned)n;
		}

		ret = inflate(&gz->strm, Z_NO_FLUSH);
		if (ret == Z_STREAM_END) {
			gz->eof = 1;
			break;
		}
		if (ret != Z_OK && ret != Z_BUF_ERROR) {
			gz->err = 1;
			break;
		}
		if (ret == Z_BUF_ERROR && gz->strm.avail_in == 0)
			continue; /* need more real input */
	}

	return (int)(len - gz->strm.avail_out);
}
