#!/bin/bash
# Builds libc_build/qos_boost.dylib (DAR-439 test aid; see qos_boost.c).
# Same recipe as build_crashdiag.sh.
set -e
cd "$(dirname "$0")"
OUT="libc_build"
SYS="$OUT/system"
XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
[ -x "$CLANG" ] && [ -d "$SDK" ] || { echo "error: Xcode-12.app not found at $XCODE12" >&2; exit 1; }
[ -f "$SYS/libSystem.B.dylib" ] || { echo "error: run build_libsystem.sh first" >&2; exit 1; }
NEWLD_BINDIR="$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain/usr/bin"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib -O1 \
    -install_name /usr/lib/qos_boost.dylib -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o "$OUT/qos_boost.dylib" qos_boost.c \
    "$SYS/libsystem_pthread.dylib" "$SYS/libsystem_kernel.dylib" "$SYS/libsystem_c.dylib" "$SYS/libdyld.dylib" "$SYS/libSystem.B.dylib"
otool -l "$OUT/qos_boost.dylib" | grep -Eq "__mod_init_func|__init_offsets" || { echo "error: no constructor section" >&2; exit 1; }
./bind_target_audit.sh "$OUT/qos_boost.dylib"
echo "wrote $OUT/qos_boost.dylib"
