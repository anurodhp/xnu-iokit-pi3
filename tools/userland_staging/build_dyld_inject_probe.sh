#!/bin/bash
# Builds libc_build/dyld_inject_probe.dylib (DAR-340/C1 regression probe, see
# dyld_inject_probe.c and docs/sudo-su-dyld-regression.md). Same real-dylib
# recipe as build_dylib_smoketest.sh.
set -e
cd "$(dirname "$0")"
OUT="libc_build"
XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
[ -x "$CLANG" ] && [ -d "$SDK" ] || { echo "error: Xcode-12.app not found at $XCODE12" >&2; exit 1; }
[ -f "$OUT/system/libSystem.B.dylib" ] || { echo "error: run build_libsystem.sh first" >&2; exit 1; }
NEWLD_BINDIR="$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain/usr/bin"
[ -x "$NEWLD_BINDIR/ld" ] || { echo "error: no ld under $NEWLD_BINDIR" >&2; exit 1; }
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/dyld_inject_probe.dylib -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o "$OUT/dyld_inject_probe.dylib" dyld_inject_probe.c "$OUT/system/libSystem.B.dylib"
otool -hv "$OUT/dyld_inject_probe.dylib" | grep -q " DYLIB " || { echo "error: not a DYLIB" >&2; exit 1; }
otool -l "$OUT/dyld_inject_probe.dylib" | grep -Eq "__mod_init_func|__init_offsets" || { echo "error: no constructor section" >&2; exit 1; }
echo "wrote $OUT/dyld_inject_probe.dylib"
