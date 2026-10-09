#!/bin/bash
# Builds libc_build/libm.a: this project's first real libm, closing the
# gap three narrow, hand-rolled math shims (ping_libm_compat.c,
# cf_compat_headers/libm_compat.c, simple_cmds_libm_compat.c) each
# independently documented as temporary because "third_party/Libm...
# genuinely absent from this checkout." Real, mostly-unmodified Apple
# source: third_party/Libm (tag Libm-2026 -- see setup_third_party.sh's
# own header comment and docs/apple-oss-catalog.md for the real
# manifest-sweep method and the honest ceiling on it: Libm was never
# listed in ANY distribution-macOS manifest checked, 10.12 through 11.2,
# and Apple's own real tag list stops at 2011, a decade before this
# project's Darwin 20.3 target and ~2 years before arm64 existed as an
# architecture at all).
#
# Real toolchain (same as every other tier in this directory): Xcode-12's
# iPhoneOS SDK, target arm64-apple-ios14.4, clang invoked directly,
# -ffreestanding -fno-builtin -fno-stack-protector -O2.
#
# Real per-file selection method: third_party/Libm/Source/ARM/*.s (real
# armv6/armv7 32-bit ARM assembly, ~40 files -- fabs, trunc, pow, sin,
# cos, tan, log, exp, atan2, etc.) is skipped ENTIRELY, not ported --
# there is no AArch64 assembly anywhere in any real Libm tag (arm64
# didn't exist yet). Source/ARM/*.c (the portable subset -- many already
# FreeBSD-derived, vendored by Apple itself under "_freeBSD.c" names) was
# individually trial-compiled against this project's real toolchain
# (real per-file loop below); 65 of 68 compiled clean with zero source
# changes. Of those 65:
#   - EXCLUDED, real reasons found by inspection, not guessed:
#     - exp2.c: compiles, but its own body is a real, confirmed Apple
#       stub -- literally `#warning exp2 is just a stub to get the test
#       linking` / `return 0.0;`. exp2_freeBSD.c (the real FreeBSD
#       implementation, also in this same tree) is used instead.
#     - nan.c: compiles, and is a REAL, correct, more-complete
#       nan()/nanf() (real tag-string parsing) than this project's
#       existing libc_printf_fp_shims.c one -- but confirmed via `nm -g`
#       against every already-built libc_build/*.a that libc_printf.a
#       already exports `_nan`/`_nanf` from that existing shim, and
#       ping.macho/other real consumers already link libc_printf.a.
#       Adding a second real definition risks a real duplicate-symbol
#       link error in any consumer that ends up needing a symbol from
#       each .o. Left out here rather than destabilize an
#       already-verified path; retiring libc_printf_fp_shims.c's
#       nan/nanf in favor of this real one is a real, separate, narrow
#       follow-up, not done as a side effect of this pass.
#     - containsNothing.c: real Apple source, but literally empty by
#       design (own comment: "This file exists merely to convince the
#       build system to build something where it is used") -- zero
#       symbols, no value pulling it in.
#   - REAL COMPILE FAILURES (2 of 68), not fixed, real reasons:
#     - version_info.c: real syntax error in this pristine checkout
#       ("expected ';' after top level declarator") -- a version-string
#       stamp helper, non-essential, not investigated further.
#     - e_rem_pio2_freeBSD.c: real "implicit declaration of
#       '__kernel_rem_pio2'" (a missing prototype, not a missing
#       symbol -- k_rem_pio2_freeBSD.c DEFINES it). Neither is built
#       any more (DAR-447): k_rem_pio2_freeBSD.c/k_tan_freeBSD.c are the
#       old fdlibm six-argument __kernel_rem_pio2 ABI
#       (k_rem_pio2_freeBSD.c:156), which the FreeBSD msun 13.2 callers
#       build_libm_dylib.sh compiles do not use; the msun k_rem_pio2.c/
#       k_tan.c are built there instead. Only needed for full-range sin/cos/tan
#       argument reduction; this real Libm tag has no portable C sin/
#       cos/tan entry points at all (real Apple source for those is
#       ARM32-assembly-only, same as fabs/trunc/pow), so nothing in this
#       archive actually needs it yet. Not fixed.
#     - lrint.c: real "header valid for __arm__ only" from its own
#       d2i.h -- a genuine 32-bit-ARM-only fixed-point-conversion
#       dependency. llround.c and llrint.c (both compile clean, same
#       real algorithmic territory at the long/long-long width) are
#       included instead.
#   - modf_IncrediblySkanky.c: compiles (silently empty without
#     __SOFTFP__), but is REAL Apple source self-described in its own
#     header comment as "Temporary softfloat implementation of modf,
#     performance is expected to be terrible" (Apple's own engineers
#     named the file "IncrediblySkanky") and needs an external trunc()
#     this project's real Apple Libm source has no portable form of
#     either. Excluded in favor of real FreeBSD modf (see below).
#   - fmod.c/fmodf.c: real Apple source, but their entire real body is
#     gated behind `#if defined(__SOFTFP__)` with no non-softfp
#     fallback (Apple's real hard-float ARM build instead links a
#     separate real fmod.s assembly file for that case, ARM32-only,
#     skipped like all the other .s files here) -- AArch64, like
#     soft-float ARM, has no hardware fmod instruction, so this project
#     compiles these two specific files WITH -D__SOFTFP__ deliberately,
#     to reach Apple's real portable bit-twiddling algorithm (verified
#     self-contained: only compiler builtins, no external calls).
#
# fabs, trunc, modf, pow, scalbn (needed internally by pow): no portable
# Apple source exists for any of the first three at all (fabs.s/trunc.s
# are ARM32-asm-only; modf's only portable form is the disowned
# "IncrediblySkanky" hack above) and pow.s/scalbn's real Apple ARM
# source is ALSO assembly-only. Per this project's own source-priority
# rule (CLAUDE.md: Apple real source first, FreeBSD real source second,
# custom only as a last resort), real, unmodified (beyond trimming the
# handful of macros they need out of FreeBSD's math_private.h)
# lib/msun/src/{s_fabs,s_trunc,s_modf,s_scalbn,e_pow}.c from
# github.com/freebsd/freebsd-src are used instead -- see
# libm_freebsd_fallback.c's own header for the full reasoning, including
# why this didn't get its own third_party/ pin (task-scoped to add
# exactly one new pin, not two, for five small self-contained files).
#
# Real dependency ordering: none -- this tier needs nothing else built
# first (self-contained against the SDK alone).
set -e
cd "$(dirname "$0")"

LIBM="../../third_party/Libm"
OUT="libc_build"
rm -rf "$OUT/libm_obj"
mkdir -p "$OUT/libm_obj"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
if [ ! -d "$LIBM/Source/ARM" ]; then
    echo "error: $LIBM not found -- run setup_third_party.sh first (pins Libm-2026)" >&2
    exit 1
fi

CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O2 \
    -Wno-nullability-completeness"

# Real per-file compile loop (build_libc_regex.sh's own pattern) over the
# curated real-Apple-source file list explained in the header comment
# above -- every exclusion there was found by actually trying to compile
# the file, not guessed.
BULK_FILES="acos_freeBSD acosh_freeBSD asin_freeBSD asinh_freeBSD atanh_freeBSD \
    cbrt complex copysignf cosh_freeBSD erf erfc erfcf erff exp_freeBSD \
    exp2_freeBSD expm1_freeBSD fdim fdimf fegetfltrounds fenv fma_freeBSD \
    fmaf fpclassify fpclassifyf ilogb ilogbf j0y0_freeBSD j1y1_freeBSD \
    jnyn_freeBSD lgamma lgammaf llrint \
    llround log logb logbf math_errhandling nearbyint nextafter \
    nextafterf nexttoward nexttowardf remainder remainderf remquo \
    remquof rint scalb scalbln scalblnf signgam sinh_freeBSD sqrt sqrtf \
    tanh_freeBSD tgamma tgammaf"

ok=0
fail=0
for base in $BULK_FILES; do
    f="$LIBM/Source/ARM/${base}.c"
    out="$OUT/libm_obj/${base}.o"
    if "$CLANG" $CFLAGS "$f" -o "$out" 2>"$OUT/libm_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/libm_obj/${base}.err"
    else
        fail=$((fail + 1))
        echo "  gap: $base -- $(grep -m1 'error:' "$OUT/libm_obj/${base}.err")"
    fi
done

# fmod/fmodf: real Apple source, but only has content with -D__SOFTFP__
# (see header comment above for why that's the right branch to take on
# this target even though it isn't literally soft-float hardware).
for base in fmod fmodf; do
    f="$LIBM/Source/ARM/${base}.c"
    out="$OUT/libm_obj/${base}.o"
    if "$CLANG" $CFLAGS -D__SOFTFP__ "$f" -o "$out" 2>"$OUT/libm_obj/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/libm_obj/${base}.err"
    else
        fail=$((fail + 1))
        echo "  gap: $base -- $(grep -m1 'error:' "$OUT/libm_obj/${base}.err")"
    fi
done

# fabs/trunc/modf/pow/scalbn: real FreeBSD fallback (see
# libm_freebsd_fallback.c's own header for why).
"$CLANG" $CFLAGS libm_freebsd_fallback.c -o "$OUT/libm_obj/libm_freebsd_fallback.o" \
    2>"$OUT/libm_obj/libm_freebsd_fallback.err" \
    && { ok=$((ok + 1)); rm -f "$OUT/libm_obj/libm_freebsd_fallback.err"; } \
    || { fail=$((fail + 1)); echo "  gap: libm_freebsd_fallback -- $(grep -m1 'error:' "$OUT/libm_obj/libm_freebsd_fallback.err")"; }

echo "libm: $ok compiled, $fail gaps (see libc_build/libm_obj/*.err for full detail)"
if [ "$fail" -ne 0 ]; then
    echo "error: libm has real, unexpected compile failures (not one of the documented ones above) -- not a clean tier" >&2
    exit 1
fi

rm -f "$OUT/libm.a"
ar rcs "$OUT/libm.a" "$OUT"/libm_obj/*.o
echo "wrote $OUT/libm.a"
nm -g "$OUT/libm.a" | awk '/:$/{next} NF==3 && $2=="T"{print $3}' | sort -u > "$OUT/libm_exports.txt"
echo "libm.a exports $(wc -l < "$OUT/libm_exports.txt" | tr -d ' ') real global functions (see libc_build/libm_exports.txt)"

# Real smoketest: exercise all three shims' real functions
# (sqrt/fmod/modf/pow/fabs/trunc/ilogb) plus a couple of the bonus real
# functions (cbrt, log10, j0), linked against Csu's real start.s/crt.c
# plus this project's existing minimal curated stack, checked via `nm -u`
# for undefined symbols -- same pattern build_libc_regex.sh's own
# smoketest uses. Degrades to a skip if the prerequisite archives aren't
# built yet.
NEED_OK=1
for a in libc_string libc_stdio libc_gen libc_printf libc_locale_for_stdio libpthread libclosure libunwind libmalloc libplatform; do
    [ -f "$OUT/$a.a" ] || NEED_OK=0
done
if [ "$NEED_OK" -eq 1 ]; then
    rm -f libm_smoketest.macho
    "$CLANG" $CFLAGS -include libc_build_prototypes.h libm_smoketest.c -o "$OUT/libm_smoketest.o"
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
        -o libm_smoketest.macho \
        ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
        "$OUT/libm_smoketest.o" "$OUT/libm.a" \
        "$OUT/libc_printf.a" "$OUT/libc_gen.a" "$OUT/libc_stdio.a" "$OUT/libc_locale_for_stdio.a" "$OUT/libc_string.a" \
        "$OUT/libunwind.a" "$OUT/libpthread.a" "$OUT/libclosure.a" \
        "$OUT/libmalloc.a" "$OUT/libplatform.a" \
        -Wl,-alias,_mprotect,'_mprotect$UNIX2003' \
        -Wl,-alias,___kill,'_kill$UNIX2003' \
        -Wl,-alias,___kill,_kill \
        -Wl,-alias,_write,'_write$UNIX2003' \
        -Wl,-alias,__mal_semwait_sleep,'_sleep$UNIX2003' \
        -Wl,-alias,___kdebug_trace,_kdebug_trace \
        -Wl,-alias,_fputs,'_fputs$UNIX2003' \
        -Wl,-alias,'_pthread_rwlock_rdlock$UNIX2003',_pthread_rwlock_rdlock \
        -Wl,-alias,'_pthread_rwlock_unlock$UNIX2003',_pthread_rwlock_unlock \
        2>"$OUT/libm_smoketest_link.err" && echo "wrote libm_smoketest.macho" || { echo "WARNING: smoketest link failed (libm.a itself is still built -- see $OUT/libm_smoketest_link.err)" >&2; }

    if [ -f libm_smoketest.macho ]; then
        otool -l libm_smoketest.macho | grep -q LC_LOAD_DYLIB && \
            echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
        otool -l libm_smoketest.macho | grep -q LC_UNIXTHREAD || \
            echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
        if nm -u libm_smoketest.macho | grep -q .; then
            echo "WARNING: undefined symbols present:" >&2
            nm -u libm_smoketest.macho >&2
        fi
    else
        tail -60 "$OUT/libm_smoketest_link.err" >&2
    fi
else
    echo "skip: libm_smoketest.macho (needs libc_string/libc_stdio/libc_gen/libc_printf/libc_locale_for_stdio/libpthread/libclosure/libunwind/libmalloc/libplatform .a -- run their build_*.sh first)"
fi
