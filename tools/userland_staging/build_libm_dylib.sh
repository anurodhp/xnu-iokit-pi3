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
# Real, honestly-classified `-Wl,-U` gap (15 symbols, found the same way
# build_libresolv_dylib.sh's own header documents: extract every member,
# union defined vs undefined across the whole archive, subtract what the
# 5 sibling dylibs below actually export -- NOT guessed). Real, confirmed
# split:
#   - atan/atan2/cos/floor/log/sin/tan/memset: real, ALREADY covered by
#     libsystem_c.dylib (confirmed via `nm -gU`) -- this project's own
#     curated libc apparently already carries double-precision base
#     transcendentals from a different real source; not part of this
#     dylib's own gap.
#   - __muldc3/__mulsc3: real compiler-rt complex-multiply intrinsics
#     (float/double), confirmed genuinely absent from this entire
#     checkout (`find third_party -iname '*muldc3*' -iname '*mulsc3*'`
#     -- zero hits) -- this project has no compiler-rt port yet, same
#     "confirmed absent, not guessed" standard as docs/backlog.md item
#     d7's sysdir_* gap.
#   - asinhf/atan2f/copysign/expf/expm1f/fabsf/hypot/hypotf/log1pf/logf/
#     scalbnf/sinf/tanf: real, genuinely-unported Libm float-precision
#     (and copysign/hypot double-precision) entry points -- confirmed
#     build_libm.sh's own compile list doesn't include the real source
#     files these need (this project's own architecture-specific Libm
#     checkout has separate Intel/PowerPC/ARM source trees, e.g.
#     third_party/Libm/Source/ARM/log.c -- porting the real ARM variants
#     is a separate, substantial Libm-completeness task, out of scope
#     for "produce a dylib from what this project has already built and
#     verified" -- not attempted here, not papered over either).
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

mkdir -p "$OUT/system"
rm -f "$OUT/system/libsystem_m.dylib"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/system/libsystem_m.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -Wl,-U,___muldc3 -Wl,-U,___mulsc3 \
    -Wl,-U,_asinhf -Wl,-U,_atan2f -Wl,-U,_copysign -Wl,-U,_expf -Wl,-U,_expm1f \
    -Wl,-U,_fabsf -Wl,-U,_hypot -Wl,-U,_hypotf -Wl,-U,_log1pf -Wl,-U,_logf \
    -Wl,-U,_scalbnf -Wl,-U,_sinf -Wl,-U,_tanf \
    -o "$OUT/system/libsystem_m.dylib" \
    "$EXTRACT"/*.o \
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
echo "declared -Wl,-U deferrals (expect: <flat-namespace> bind target, resolved at real load time):"
xcrun dyld_info -fixups "$OUT/system/libsystem_m.dylib" 2>/dev/null | grep '<flat-namespace>' | awk '{print "  " $NF}'
UNEXPECTED=$(xcrun dyld_info -fixups "$OUT/system/libsystem_m.dylib" 2>/dev/null | grep -v -E '<flat-namespace>|<weak-def-coalesce>' | grep 'bind' | grep -v -E 'libsystem_c/|libsystem_malloc/|libsystem_platform/|libsystem_kernel/|libsystem_pthread/' || true)
if [ -n "$UNEXPECTED" ]; then
    echo "error: real, unexpected non-sibling bind targets found:" >&2
    echo "$UNEXPECTED" >&2
    exit 1
fi
echo "libsystem_m.dylib: every non-deferred external reference binds to a real, named sibling dylib -- no unexpected/dangling references"
