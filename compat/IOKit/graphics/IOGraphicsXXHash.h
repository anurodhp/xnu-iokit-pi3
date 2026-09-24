/*
 * IOGraphicsXXHash.h -- real upstream xxHash (Cyan4973/xxHash v0.8.2,
 * third_party/xxHash, pinned in setup_third_party.sh) configured for a
 * kernel C++ translation unit, for IODisplay.cpp's one XXH64() call.
 *
 * WHY (DAR-331). IODisplay.cpp:662 hashes the display's EDID with
 * XXH64() and IODisplay.cpp:441-442 formats that hash into the
 * display-preferences key ("-%x-%x-%llx"), but the IOGraphics-585 drop
 * never includes or ships xxhash.h -- Apple vendors it internally. Not
 * Apple's code, so per the source-priority rule this is the real
 * upstream, unmodified; only its documented configuration macros are
 * set here.
 *
 * CONFIGURATION -- upstream's own freestanding/kernel mode, each macro
 * cited against the pinned xxhash.h:
 *   XXH_INLINE_ALL   (:225-231) every symbol static inline in this TU;
 *                    nothing exported to collide with libkern or a kext.
 *   XXH_NO_STDLIB    (:2044-2053) "useful in situations where xxhash.h
 *                    is integrated into some kernel" -- no <stdlib.h>;
 *                    XXH_malloc returns NULL. Only the *_createState()
 *                    APIs use it, and nothing here calls them.
 *   XXH_NO_XXH3      (:993, :3375) drops XXH3/XXH128 entirely, and with
 *                    it every SIMD include (<arm_neon.h>, :3434). Kernel
 *                    code must not touch the FP/SIMD file unsaved;
 *                    XXH64 is scalar and does not depend on XXH3.
 *   XXH_NO_STREAM    (:577 etc.) no streaming state API -- unused.
 *   XXH_DEBUGLEVEL=0 (:2150) keeps <assert.h> out.
 * What remains is <string.h> (memcpy, :2080), <limits.h> (:2091) and
 * <stdint.h>, all provided by the kernel's own headers.
 *
 * WHY v0.8.2 AND NOT AN ERA-MATCHED v0.8.0: v0.8.0 unconditionally
 * includes <stdlib.h> and, on arm64, <arm_neon.h>; XXH_NO_STDLIB first
 * appears in v0.8.2. XXH64's output is fixed by the algorithm
 * (doc/xxhash_spec.md), so the version does not change the hash.
 */

#ifndef _IOKIT_IOGRAPHICS_XXHASH_H
#define _IOKIT_IOGRAPHICS_XXHASH_H

#define XXH_INLINE_ALL
#define XXH_NO_STDLIB
#define XXH_NO_XXH3
#define XXH_NO_STREAM
#define XXH_DEBUGLEVEL 0

#include <xxhash.h>

#ifdef __cplusplus
/*
 * ARITY GAP. IODisplay.cpp:662 calls XXH64(data, len) -- two arguments.
 * Every upstream xxHash since the XXH64 API was introduced takes three,
 * (input, length, seed) (xxhash.h XXH64 prototype), so Apple's internal
 * copy is a variant with an implied seed that was never published.
 * This overload supplies seed 0 and does not edit Apple's call site.
 * Basis: doc/xxhash_spec.md:214 calls XXH64's seed "optional, it can
 * be `0`", and :367 names 0 as the default seed for XXH3 -- i.e. 0 is
 * upstream's own "no seed" value. It is the natural reading of a
 * seedless wrapper, not a verified fact about Apple's.
 *
 * Whether 0 is Apple's seed cannot be verified, and on this board it
 * cannot matter: the hash is only formatted into the preferences key
 * when the framebuffer has an IOPCIDevice ancestor (IODisplay.cpp:
 * 410-420, else useOldPrefs = true and the hashed key is never built).
 * BCM2837 has no PCI. If a PCI framebuffer ever exists here, the only
 * effect of a seed mismatch is that preferences saved by real macOS
 * under the hashed key are not found -- and there is no real macOS
 * preference store on this project to find them in.
 *
 * XXH64 is a macro here (XXH_INLINE_ALL renames it to XXH_INLINE_XXH64,
 * xxhash.h:402); overloading the renamed function keeps the call site
 * `XXH64(a, b)` resolving to this.
 */
static inline XXH64_hash_t
XXH_INLINE_XXH64(const void* input, size_t length)
{
	return XXH_INLINE_XXH64(input, length, 0);
}
#endif

#endif /* _IOKIT_IOGRAPHICS_XXHASH_H */
