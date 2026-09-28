#!/bin/bash
# Builds libc_build/ffi_smoketest.macho (DAR-388): ffi_call plus an ffi
# closure against libffi.dylib (build_libffi.sh). See ffi_smoketest.c.
set -euo pipefail
cd "$(dirname "$0")"
OUT="libc_build"
CSU="../../third_party/Csu"
XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
for f in system/libffi.dylib system/libffi-trampolines.dylib libffi_include/ffi.h; do
    [ -e "$OUT/$f" ] || { echo "error: $OUT/$f not found -- run build_libffi.sh first" >&2; exit 1; }
done
# Hosted compile (printf/strlen), same recipe as x11_common.sh's
# X11_CFLAGS: -D_FORTIFY_SOURCE=0 keeps the SDK from rewriting calls into
# __*_chk variants.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -O0 -Wall \
    -fno-stack-protector -D_FORTIFY_SOURCE=0 -I "$OUT/libffi_include" \
    '-DSPI_AVAILABLE(...)=' '-DSPI_DEPRECATED(...)=' \
    '-D__API_AVAILABLE_PLATFORM_iosmac(x)=macCatalyst,introduced=x' \
    '-D__API_DEPRECATED_PLATFORM_iosmac(x,y)=macCatalyst,introduced=x,deprecated=y' \
    ffi_smoketest.c -o "$OUT/ffi_smoketest.o"
M="$OUT/ffi_smoketest.macho"
rm -f "$M"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" -Wl,-fixup_chains -o "$M" \
    "$CSU/start.s" "$CSU/crt.c" "$OUT/ffi_smoketest.o" \
    "$OUT/system/libffi.dylib" \
    "$OUT/system/libsystem_kernel.dylib" "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libsystem_malloc.dylib" "$OUT/system/libsystem_platform.dylib" \
    "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib"
otool -l "$M" | grep -q "cmd LC_MAIN" || { echo "error: $M has no LC_MAIN" >&2; exit 1; }
./bind_target_audit.sh "$M"
echo "wrote $M"
