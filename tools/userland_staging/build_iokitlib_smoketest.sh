#!/bin/bash
# Builds libc_build/iokitlib_smoketest.macho -- DAR-251's check of
# libiokituser.dylib's real IOKitLib API (IOCFSerialize XML and binary,
# IOMasterPort, IOServiceGetMatchingServices over IONetworkInterface,
# IONotificationPort + its run-loop source, CFRunLoopGetCurrent on a
# created thread). See iokitlib_smoketest.c.
set -euo pipefail
cd "$(dirname "$0")"
OUT="libc_build"
T="../../third_party"
XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
NEWLD_BINDIR="$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain/usr/bin"
S="$OUT/system"
for f in libiokituser libCoreFoundation libsystem_kernel libsystem_c libsystem_pthread \
         libsystem_malloc libsystem_platform libdyld libSystem.B; do
    if [ ! -f "$S/$f.dylib" ]; then
        echo "error: $S/$f.dylib not found -- build it first" >&2
        exit 1
    fi
done
if [ ! -d "$OUT/iokitlib_headers" ]; then
    echo "error: $OUT/iokitlib_headers not found -- run build_libiokituser_dylib.sh first" >&2
    exit 1
fi
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -O0 -Wno-nullability-completeness -DPRIVATE=1 \
    -I "$OUT/iokitlib_headers" -I "$OUT/compat_include" -I "$OUT/cf_headers" \
    -I "$T/xnu-7195/iokit" -I "$T/xnu-7195/libkern" -idirafter "$T/xnu-7195/osfmk" \
    iokitlib_smoketest.c -o "$OUT/iokitlib_smoketest.o"
M="$OUT/iokitlib_smoketest.macho"
rm -f "$M"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o "$M" "$T/Csu/start.s" "$T/Csu/crt.c" "$OUT/iokitlib_smoketest.o" \
    "$S/libiokituser.dylib" "$S/libCoreFoundation.dylib" "$S/libsystem_kernel.dylib" \
    "$S/libsystem_c.dylib" "$S/libsystem_pthread.dylib" "$S/libsystem_malloc.dylib" \
    "$S/libsystem_platform.dylib" "$S/libdyld.dylib" "$S/libSystem.B.dylib"
otool -l "$M" | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || { echo "error: no LC_LOAD_DYLINKER" >&2; exit 1; }
otool -l "$M" | grep -q "cmd LC_LOAD_DYLIB" || { echo "error: no LC_LOAD_DYLIB" >&2; exit 1; }
otool -l "$M" | grep -q "cmd LC_MAIN" || { echo "error: no LC_MAIN" >&2; exit 1; }
./bind_target_audit.sh "$M" || { echo "error: $M has an unresolvable two-level bind" >&2; exit 1; }
echo "wrote $M"
