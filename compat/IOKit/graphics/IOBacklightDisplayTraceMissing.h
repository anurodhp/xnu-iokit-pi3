/*
 * IOBacklightDisplayTraceMissing.h -- the Adaptive-Backlight (ABL)
 * telemetry macros IODisplay.cpp uses and the IOGraphics drop never
 * defines.
 *
 * WHY THIS FILE EXISTS (DAR-331)
 *
 * IODisplay.cpp:38 includes <IOKit/graphics/IOBacklightDisplayTrace.h>,
 * whose published body is empty (14 lines: a guard and one include of
 * GTraceTypes.hpp). The .cpp then uses ABLTRACE_RAW, six ABL_GT_*
 * instance macros, five ABL_* function IDs and four ABL_SOURCE_* source
 * IDs. Checked every one of the 80 published IOGraphics tags
 * (IOGraphics-16 .. -600) with `git grep`: none defines ABLTRACE_RAW or
 * any ABL_GT_* macro, and IOBacklightDisplayTrace.h never has a single
 * ABL line in any of them. Not a bad pin; the header is simply stripped.
 *
 * WHAT THEY ARE. Trace-record emitters, nothing else:
 *   - IODisplay.cpp:1269-1316 defines local `staticABL_GT_*` copies of
 *     the missing instance macros ("setParameter is a static function
 *     and can't call the standard ABL_GT_.*() macros. I have to
 *     re-implement them here locally"). Those copies show the whole
 *     contract: pack integers into a uint64_t, then hand eight words to
 *     ABLTRACE_RAW. No return value, no out-parameter, no state read
 *     back.
 *   - Every argument at all 9 ABL_GT_* call sites (IODisplay.cpp:753,
 *     1461-1477, 1546) and all 6 staticABL_GT_* call sites
 *     (IODisplay.cpp:1340-1358) is a local variable, literal or `this`
 *     -- no side effects to preserve.
 *   - The ABL_* / ABL_SOURCE_* identifiers appear only as arguments to
 *     these macros (GTFuncTag()'s fid, or a GPACKUINT8T() field), never
 *     in a comparison, struct, property or selector.
 *
 * WHY A NO-OP SINK, NOT gGTrace. The ABL prefix and its own function-ID
 * space mean these records go to a separate backlight trace buffer, not
 * IOGraphics' own gGTrace (IOGraphicsKTrace.h:55) -- routing them there
 * with invented IDs would corrupt that buffer's decoding. This project
 * ships no decoder for either (same documented gap as the no-op
 * gTraceData/agdcTraceData breadcrumbs in tools/patch_iographics_gaps.sh
 * fix 3). So ABLTRACE_RAW evaluates and discards its arguments; the
 * staticABL_GT_* packing code in IODisplay.cpp still compiles as Apple
 * wrote it.
 *
 * The ID values below are this project's, not Apple's, and are never
 * observable. If a future drop publishes the real header, delete this
 * file rather than merging it.
 *
 * Force-included by tools/iographics_gap_audit.sh; any vendoring into
 * third_party/xnu-7195 must arrange the same include.
 */

#ifndef _IOKIT_IOBACKLIGHTDISPLAYTRACE_MISSING_H
#define _IOKIT_IOBACKLIGHTDISPLAYTRACE_MISSING_H

/* Function IDs -- GTFuncTag()'s first argument (IODisplay.cpp:1273). */
#define ABL_SET_DISPLAY                     1   // IODisplay.cpp:753
#define ABL_SET_BRIGHTNESS                  2   // IODisplay.cpp:1294
#define ABL_SET_BRIGHTNESS_PROBE            3   // IODisplay.cpp:1297
#define ABL_SET_DISPLAY_POWER               4   // IODisplay.cpp:1306
#define ABL_COMMIT                          5   // IODisplay.cpp:1315
#define ABL_DO_UPDATE                       6   // IODisplay.cpp:1546

/* Source IDs -- the `where` byte, GPACKUINT8T(1, where) (IODisplay.cpp:1287). */
#define ABL_SOURCE_IOD_ADDPARAMETERHANDLER  1   // IODisplay.cpp:753
#define ABL_SOURCE_IOD_SETPARAMETER         2   // IODisplay.cpp:1341
#define ABL_SOURCE_IOD_DOINTEGERSET         3   // IODisplay.cpp:1461
#define ABL_SOURCE_IOD_DOUPDATE             4   // IODisplay.cpp:1546

/*
 * Eight words, same shape as GTRACERAW()'s token (GTrace.hpp:46-52):
 * tag+arg0, then three tag/arg pairs. Arguments are evaluated (they are
 * side-effect-free, see above) so every local the staticABL_GT_* bodies
 * compute stays "used".
 */
#define ABLTRACE_RAW(t0, a0, t1, a1, t2, a2, t3, a3) do {                   \
    (void)(t0); (void)(a0); (void)(t1); (void)(a1);                         \
    (void)(t2); (void)(a2); (void)(t3); (void)(a3);                         \
} while (0)

/*
 * Instance forms. Arities pinned by their call sites; argument meaning
 * for the ones mirrored by a staticABL_GT_* twin is given there.
 */
#define ABL_GT_SET_DISPLAY(disp, where, a, b, c)                            \
    do { (void)(disp); (void)(where); (void)(a); (void)(b); (void)(c);      \
    } while (0)
#define ABL_GT_SET_BRIGHTNESS(disp, where, b, lb, isLinear)                 \
    ABL_GT_SET_DISPLAY(disp, where, b, lb, isLinear)
#define ABL_GT_SET_BRIGHTNESS_PROBE(disp, where, b, lb, isLinear)           \
    ABL_GT_SET_DISPLAY(disp, where, b, lb, isLinear)
#define ABL_GT_COMMITTED(disp, where, lb, nvr, asu)                         \
    ABL_GT_SET_DISPLAY(disp, where, lb, nvr, asu)
#define ABL_GT_SET_DISPLAY_POWER(disp, where, newVal, oldVal, ind)          \
    ABL_GT_SET_DISPLAY(disp, where, newVal, oldVal, ind)
#define ABL_GT_DO_UPDATE(disp, where, ok, count)                            \
    ABL_GT_SET_DISPLAY(disp, where, ok, count, 0)

#endif /* _IOKIT_IOBACKLIGHTDISPLAYTRACE_MISSING_H */
