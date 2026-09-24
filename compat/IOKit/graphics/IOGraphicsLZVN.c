/*
 * IOGraphicsLZVN.c -- the 4-argument lzvn_decode() IOGraphics expects,
 * built on Apple's own open-source LZVN decoder (DAR-331).
 *
 * WHAT IS MISSING. IOFramebuffer.cpp:2317 declares
 *
 *     extern "C" size_t lzvn_decode(void *dst, size_t dst_size,
 *                                   const void *src, size_t src_size);
 *
 * and :2390 uses it to unpack the LZVN-packed boot Apple logo
 * (AppleLogo.h/AppleLogo2X.h), keeping the logo only if the return
 * value equals the full unpacked size. On macOS this is a kernel export;
 * xnu-7195's published source contains no LZVN code at all.
 *
 * WHERE THE REAL CODE COMES FROM. Apple's LZFSE reference implementation
 * (third_party/lzfse, github.com/lzfse/lzfse @ lzfse-1.0, BSD 3-Clause,
 * Apple Inc.) contains the real LZVN decoder, lzvn_decode_base.c, and it
 * is compiled unmodified. Its entry point is state-based,
 * `void lzvn_decode(lzvn_decoder_state *)` (lzvn_decode_base.h:66).
 * lzfse_internal.h:413 declares exactly the one-shot shape IOGraphics
 * wants, as `lzvn_decode_buffer(dst, dst_size, src, src_size)`, but no
 * definition exists anywhere in that repository, back to its initial
 * commit. This file is that missing wrapper.
 *
 * HOW IT IS BUILT. It drives the state decoder exactly the way lzfse's
 * own LZFSE_COMPRESSEDLZVN_BLOCK_MAGIC case does (lzfse_decode_base.c:
 * 577-597): zero the state, set src/src_end and dst_begin/dst/dst_end,
 * d_prev = 0, end_of_stream = 0, call the decoder, and report
 * dst - dst_begin as the number of bytes produced. The decoder stops
 * at end-of-stream, at a full destination, or on invalid input; in the
 * last case the short count makes IOGraphics skip the logo, as it does
 * on macOS.
 *
 * THE NAME CLASH. The state function and the symbol IOGraphics wants are
 * both called `lzvn_decode`, and C cannot overload. The vendoring script
 * compiles lzvn_decode_base.c and this file with
 * -Dlzvn_decode=lzfse_lzvn_decode_state, so the state function becomes
 * lzfse_lzvn_decode_state. The #undef below then frees the real name for
 * IOGraphics' 4-argument ABI.
 */

#include "lzvn_decode_base.h"   /* declares lzfse_lzvn_decode_state via the -D rename */
#include <string.h>

#undef lzvn_decode

size_t lzvn_decode(void *dst, size_t dst_size, const void *src, size_t src_size);

size_t
lzvn_decode(void *dst, size_t dst_size, const void *src, size_t src_size)
{
	lzvn_decoder_state dstate;

	memset(&dstate, 0x00, sizeof(dstate));
	dstate.src = (const unsigned char *)src;
	dstate.src_end = (const unsigned char *)src + src_size;
	dstate.dst_begin = (unsigned char *)dst;
	dstate.dst = (unsigned char *)dst;
	dstate.dst_end = (unsigned char *)dst + dst_size;
	dstate.d_prev = 0;
	dstate.end_of_stream = 0;

	lzfse_lzvn_decode_state(&dstate);

	return (size_t)(dstate.dst - dstate.dst_begin);
}
