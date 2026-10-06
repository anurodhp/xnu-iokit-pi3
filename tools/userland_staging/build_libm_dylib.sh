#!/bin/bash
# Builds libc_build/system/libsystem_m.dylib: static-to-dylib migration,
# docs/backlog.md item d8's "real, standalone libraries still static,
# genuine conversion candidates" list (docs/daemon-roadmap.md's
# "Static-vs-dylib architecture debt" section). Real Apple install path:
# /usr/lib/system/libsystem_m.dylib -- unlike libresolv/libiconv/libxml2
# (real, standalone top-level /usr/lib/ dylibs), Libm really is one of
# Apple's own libSystem umbrella sub-dylibs on real macOS (same family as
# libsystem_c.dylib/libsystem_malloc.dylib already in this project) --
# matched here rather than a top-level /usr/lib/libm.dylib compat-shim
# path, consistent with how this project already installs every other
# libsystem_* sibling.
#
# Real, unmodified Apple/FreeBSD source: reuses every already-built,
# already-verified member object out of libc_build/libm.a (ar x, not
# recompiled -- see build_libm.sh's own header for the full scope
# writeup: this project's own curated Libm tier, mostly real FreeBSD
# msun-derived inverse-trig/hyperbolic bodies, not re-derived here).
#
# DAR-124/DAR-16, 2026-08-31 sweep continuation: this dylib used to ship
# with a real, honestly-classified 15-symbol `-Wl,-U` flat-namespace
# deferral (__muldc3/__mulsc3 -- real compiler-rt complex-multiply
# intrinsics, confirmed genuinely absent from this checkout -- plus 13
# unported float-precision entries: asinhf/atan2f/copysign/expf/expm1f/
# fabsf/hypot/hypotf/log1pf/logf/scalbnf/sinf/tanf). Round 13 of that
# sweep already found this class of deferral is NOT safe under this
# project's eager `-Wl,-fixup_chains` convention (a real PID-1 boot panic,
# "symbol not found in flat namespace '__muldc3'", the instant
# libsystem_m.dylib became a required load dependency via a
# libSystem.B.dylib reexport attempt -- see docs/static-linking-cleanup-
# plan.md's "continuation 13" and that fix's revert).
#
# What this round found, by tracing every one of those 15 undefined
# references to its real source object (not guessed): ALL 15 come from
# exactly two objects, `complex.o` (the real FreeBSD C99 <complex.h>
# family -- cabs/cacos/casin/catan/ccos/cexp/clog/conj/cpow/cproj/csin/
# csqrt/ctan + float variants, 44 exported symbols) and `scalblnf.o`
# (defines only `scalblnf`, which calls `scalbnf` internally). Confirmed,
# not assumed: a real grep across every real consumer in this checkout
# (third_party/PureDarwin, third_party/dyld, third_party/libxml2, this
# project's own tools/) found ZERO callers of any complex-number function
# or of `scalblnf` anywhere. Confirmed further: no OTHER object inside
# libm.a itself has an undefined reference to anything complex.o/
# scalblnf.o define (checked all 45 of their exported symbols against
# every other extracted .o's own undefined-symbol list) -- both are
# genuinely dead weight for this project, not merely unexported.
#
# Real fix, not a workaround: exclude complex.o/scalblnf.o from this
# dylib's own link (below) instead of porting the missing intrinsics.
# This is possible specifically because nothing needs them -- if a real
# future consumer ever needs complex-number support, the __muldc3/
# __mulsc3 porting task described in the prior version of this comment
# (and docs/static-linking-cleanup-plan.md's "continuation 13" writeup)
# is still the honest way to bring complex.o back safely. Confirmed the
# two real consumers that actually load this dylib today
# (`libxml2.dylib`, which only needs `_fmod`/`_log10`/`_pow`; and
# `df.macho`, part of build_simple_cmds.sh, which needs `_fabs`/`_trunc`
# -- both already provided by libsystem_c.dylib -- plus `_ilogb`, which
# this dylib's remaining, unmodified real objects still export) neither
# reference anything complex.o/scalblnf.o provided.
#
# Result: this dylib now has ZERO flat-namespace deferrals (confirmed via
# `xcrun dyld_info -fixups` below) -- a real, complete DAR-16 fix, not
# just a documented gap. `atan/atan2/cos/floor/log/sin/tan/memset` remain
# ALREADY covered by libsystem_c.dylib (confirmed via `nm -gU`, unrelated
# to this change) -- this project's own curated libc carries
# double-precision base transcendentals from a different real source;
# that overlap is DAR-160's own separate, still-open duplicate-symbol
# question, not a gap.
set -e
cd "$(dirname "$0")"

OUT="libc_build"
EXTRACT="$OUT/libm_dylib/extracted"
rm -rf "$EXTRACT"
mkdir -p "$EXTRACT"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
if [ ! -f "$OUT/libm.a" ]; then
    echo "error: $OUT/libm.a not found -- run build_libm.sh first (this script reuses its already-built member objects)" >&2
    exit 1
fi
for d in libsystem_c libsystem_malloc libsystem_platform libsystem_kernel libsystem_pthread; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2
        exit 1
    fi
done

# Real, verified fix (2026-08-28, see docs/backlog.md item d9 for the
# full trail): Xcode 12's own bundled ld64-609 has a real internal crash
# under -Wl,-fixup_chains that a much newer ld does not. Compiling still
# uses Xcode 12's clang/SDK above; only the final link step's linker
# binary changes, via clang's -B override.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
    exit 1
fi

(cd "$EXTRACT" && ar x "../../libm.a")

# DAR-124/DAR-16 fix (see header comment): drop the two real, genuinely
# dead objects that were the sole source of every flat-namespace
# deferral this dylib used to carry -- no in-project consumer, real or
# in-archive, needs complex-number support or scalblnf().
rm -f "$EXTRACT/complex.o" "$EXTRACT/scalblnf.o"

# Real FreeBSD msun (third_party/freebsd-src, release/13.2.0,
# lib/msun/src; setup_third_party.sh's freebsd-src sparse list) for five
# functions no dylib here exported and Apple's pinned Libm-2026 has only
# as ARM32 assembly (Source/ARM/{ceilf,sinf,cosf,frexp,hypot}.s): ceilf,
# sinf, cosf, frexp and hypot. DAR-384 (font/2D stack): cairo calls all
# five (cairo-matrix.c, cairo-arc.c, cairo-ft-font.c, ...). This is the
# "dedicated FreeBSD msun pin" libm_freebsd_fallback.c's header says a
# broader port should use, instead of more transcription.
#   - Compiled unmodified, from a copy of lib/msun/src without its own
#     math.h: every msun file does `#include "math.h"`, and FreeBSD's
#     math.h (machine/_limits.h, FreeBSD cdefs) cannot build against the
#     Darwin SDK. Without it the quoted include falls through to the SDK's
#     <math.h>, which declares the same C99 functions.
#   - -include libm_msun_compat.h: the few FreeBSD cdefs/_types spellings
#     msun needs, each cited to its FreeBSD header line in that file.
#   - s_sinf.c/s_cosf.c #include k_sinf.c, k_cosf.c and e_rem_pio2f.c
#     (INLINE_KERNEL_*), and s_sin.c/s_cos.c/s_tan.c #include e_rem_pio2.c;
#     their large-argument path calls __kernel_rem_pio2, msun's own
#     k_rem_pio2.c below. (Until DAR-447 it was libm.a's Apple
#     k_rem_pio2_freeBSD.o, which is NOT the same file: it is the old
#     fdlibm six-argument ABI, k_rem_pio2_freeBSD.c:156, while msun 13.2
#     calls five arguments, math_private.h:887. sin(1e9) came out 0.736
#     instead of 0.546 and sin(1e22) crashed.)
#   - s_frexp.c and e_hypot.c also emit their long double aliases (frexpl,
#     hypotl): long double is double on Darwin arm64 (LDBL_MANT_DIG 53).
#     e_hypot.c:73 calls fabsl, exported below as an alias of fabs
#     (libm_freebsd_fallback.o, in this link), as Apple's libm does.
MSUN="../../third_party/freebsd-src/lib/msun/src"
if [ ! -f "$MSUN/s_sinf.c" ]; then
    echo "error: $MSUN not found -- run setup_third_party.sh (freebsd-src sparse checkout needs lib/msun/src)" >&2
    exit 1
fi
MSUN_COPY="$OUT/libm_dylib/msun_src"
rm -rf "$MSUN_COPY"
mkdir -p "$MSUN_COPY"
cp "$MSUN"/*.c "$MSUN/math_private.h" "$MSUN_COPY/"
# e_powf.c (DAR-424): powf, for Window Maker's wrlib (gradient/alpha
# math). msun's own float pow, same shape as s_sinf/s_cosf above; it
# calls fabsf and scalbnf, which s_fabsf.c/s_scalbnf.c provide.
# e_atan2f.c + s_atanf.c: atan2f (wmaker's src/ geometry), same source.
# DAR-447: sin, cos, tan, atan, atan2, log, ceil, floor and round, which
# libsystem_c.dylib used to export as hand-written approximations
# (libsystem_c_gap_compat.c, off by up to ~7e12 ulp), with msun's
# k_sin/k_cos/k_tan/k_rem_pio2 kernels; and floorf/rintf (gnustep-gui,
# DAR-390). libm_accuracy_smoketest.c checks them on the target.
for f in s_ceilf s_sinf s_cosf s_frexp e_hypot e_powf s_fabsf s_scalbnf e_atan2f s_atanf \
         s_sin s_cos s_tan k_sin k_cos k_tan k_rem_pio2 s_atan e_atan2 e_log s_ceil s_floor s_round \
         s_floorf s_rintf; do
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
        -Wno-nullability-completeness -include libm_msun_compat.h \
        "$MSUN_COPY/$f.c" -o "$EXTRACT/msun_$f.o"
done

# DARW-290: the ~60 libm functions libsystem_m lacked (found building ICU 74 / WebKit for
# Florence: expf, logf, log2, tanf, cbrtf, roundf, truncf, copysign, lrint, ...), from the same
# msun sources and recipe as above. The ones msun cannot compile here (s_fmax wants FreeBSD's
# fpmath.h, s_lround sys/limits.h, s_lrintf a templated math.h clash) and the fenv flag functions
# are libm_c99_extras.c. msun's own math.h stays out so Darwin's is used. Long-double variants
# are not built (long double is double on arm64, nothing here needs them yet).
MSUN_NEW="$OUT/libm_dylib/msun_new"
rm -rf "$MSUN_NEW"; mkdir -p "$MSUN_NEW"
cp "$MSUN"/*.c "$MSUN"/*.h "$MSUN_NEW/"; rm -f "$MSUN_NEW/math.h"
for f in e_expf e_logf e_log2 e_log2f e_log10f s_exp2f s_expm1f s_log1pf s_tanf e_asinf e_acosf \
         s_cbrtf e_hypotf s_roundf s_truncf s_frexpf s_modff s_copysign s_lrint e_rem_pio2f k_rem_pio2; do
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
        -Wno-everything -I "$MSUN_NEW" -include libm_msun_compat.h \
        "$MSUN_NEW/$f.c" -o "$EXTRACT/msun_$f.o"
done
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-everything libm_c99_extras.c -o "$EXTRACT/libm_c99_extras.o"

mkdir -p "$OUT/system"
rm -f "$OUT/system/libsystem_m.dylib"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/system/libsystem_m.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o "$OUT/system/libsystem_m.dylib" \
    "$EXTRACT"/*.o \
    -Wl,-alias,_fabs,_fabsl \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_pthread.dylib" \
    2>"$OUT/libm_dylib_link.err" \
    && echo "wrote $OUT/system/libsystem_m.dylib" \
    || { echo "libsystem_m.dylib link found real undefined symbols:" >&2; grep '^  "' "$OUT/libm_dylib_link.err" | sed 's/,.*//' | sort -u >&2; exit 1; }

otool -hv "$OUT/system/libsystem_m.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
# See build_libresolv_dylib.sh's own verification comment for why this
# checks dyld_info -fixups bind targets rather than plain `nm -u`
# emptiness.
echo "declared -Wl,-U deferrals (expect: NONE -- see DAR-124/DAR-16 header comment, complex.o/scalblnf.o removal closed all 15):"
FLATNS=$(xcrun dyld_info -fixups "$OUT/system/libsystem_m.dylib" 2>/dev/null | grep '<flat-namespace>' | awk '{print "  " $NF}')
echo "$FLATNS"
if [ -n "$FLATNS" ]; then
    echo "error: real, unexpected flat-namespace deferral(s) reappeared -- this dylib is supposed to have zero (DAR-124/DAR-16 fix, 2026-08-31); investigate before shipping, do not just re-add -Wl,-U" >&2
    exit 1
fi
UNEXPECTED=$(xcrun dyld_info -fixups "$OUT/system/libsystem_m.dylib" 2>/dev/null | grep -v -E '<flat-namespace>|<weak-def-coalesce>' | grep 'bind' | grep -v -E 'libsystem_c/|libsystem_malloc/|libsystem_platform/|libsystem_kernel/|libsystem_pthread/' || true)
if [ -n "$UNEXPECTED" ]; then
    echo "error: real, unexpected non-sibling bind targets found:" >&2
    echo "$UNEXPECTED" >&2
    exit 1
fi
echo "libsystem_m.dylib: every non-deferred external reference binds to a real, named sibling dylib -- no unexpected/dangling references"
