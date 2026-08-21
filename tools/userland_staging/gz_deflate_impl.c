/* Real DEFLATE (compression) core for gzip(1) -- the missing half the
 * grep/sed/nl/xargs/ed sweep's own gz_gzip_impl.c already documented
 * ("only the deflate/compress side and a CLI wrapper are still missing,
 * not the whole library"). Real, unmodified upstream zlib compression
 * source (third_party/u-boot/lib/zlib/{deflate,trees}.c -- u-boot
 * vendors both, not just the inflate side it actually uses itself,
 * confirmed by directory listing, not assumed) copied by
 * build_simple_cmds.sh into $OUT/zlib_glue/ next to the same real
 * inflate-side files gz_gzip_impl.c already established, sharing the
 * exact same zutil.h/deflate.h/trees.h-resolves-via-own-directory
 * mechanism (see gz_gzip_impl.c's own header for the full story on why
 * quote-form `#include "zutil.h"` needs this).
 *
 * Unlike grep's own gz_gzip_impl.c (which wraps the real inflate() state
 * machine behind a narrow gzdopen()/gzread() surface because grep.c/
 * file.c only ever call those two), real, unmodified file_cmds/gzip/
 * gzip.c calls the lower-level public zlib API directly -- z_stream,
 * deflateInit2()/deflate()/deflateEnd(), inflateInit2()/inflate()/
 * inflateEnd() -- so this file does NOT add another wrapper layer; it
 * just compiles the real codec (both directions) as one unity
 * translation unit (same "#include the real .c files directly" style
 * u-boot's own lib/zlib/zlib.c already uses upstream, not invented
 * here) so gzip.macho's own real calls to those exact real symbol names
 * resolve for real. crc32() (deflate.c's real gzip-format wrap==2 path,
 * and gzip.c's own trailer-CRC bookkeeping) is the same real, standalone
 * gz_crc32_compat.c every other gzip-adjacent command already links --
 * NOT re-included here (build_simple_cmds.sh adds it as its own separate
 * CMD_LIST extra source for gzip, avoiding a duplicate-symbol risk if
 * this file were to pull it in too).
 *
 * This is a completely separate object from grep's own gz_gzip_impl.c
 * (different binary, gzip.macho vs grep.macho, no link-time interaction
 * whatsoever) -- there is no risk of the real inflate()/deflate() symbol
 * definitions colliding across binaries that never link together. */
#define ZLIB_INTERNAL
#include "zutil.h"
#include "deflate.h"
#include "inftrees.h"
#include "inflate.h"
#include "inffast.h"
#include "inffixed.h"
#include "trees.c"
#include "deflate.c"
#include "inffast.c"
#include "inftrees.c"
#include "inflate.c"
#include "zutil.c"
#include "adler32.c"
