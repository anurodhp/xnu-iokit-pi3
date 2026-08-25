#!/bin/bash
# Phase 6.5 Phase A (docs/daemon-roadmap.md): builds libtest.dylib, a real
# .dylib produced by this project's pinned Xcode-12 ld64 -- the first dylib
# this toolchain has ever emitted. Every other build_*.sh here targets
# -static output on purpose (see CLAUDE.md); this one is the first
# deliberate step off that path, additive only -- it does not touch any
# existing static script.
#
# Purely a load-command-shape proof (MH_DYLIB / LC_ID_DYLIB); no real dyld
# exists yet to ever load this file, so it is not installed onto any SD
# image. build_dyld_test_exec.sh links against it next.
set -e
cd "$(dirname "$0")"

OUT="libc_build"
mkdir -p "$OUT"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi

# -Wl,-fixup_chains: real dyld-940 (this project's currently pinned
# tag) expects the modern chained-fixups format -- see
# build_dyld_test_exec.sh's own header comment for the full real
# crash this fixed (EXC_BAD_ACCESS, fault address 0x10, in real dyld's
# JustInTimeLoader bind/fixup code against the older LC_DYLD_INFO_ONLY
# bind-opcode format ld64 emits by default).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib \
    -install_name /usr/lib/libtest.dylib \
    -Wl,-fixup_chains \
    -o "$OUT/libtest.dylib" libtest_dylib.c
echo "wrote $OUT/libtest.dylib"

otool -hv "$OUT/libtest.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB -- ld64 did not produce a real dylib" >&2
    exit 1
}
otool -l "$OUT/libtest.dylib" | grep -q LC_ID_DYLIB || {
    echo "error: no LC_ID_DYLIB -- not a real dylib" >&2
    exit 1
}
echo "confirmed: $OUT/libtest.dylib is a real MH_DYLIB with LC_ID_DYLIB"
