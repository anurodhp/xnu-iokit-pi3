#!/bin/bash
# Builds /usr/sbin/bt (bt.c), the userland end of BCM43430Bluetooth
# (docs/bluetooth-plan.md). Same link shape as build_audio_play.sh: a dynamic
# executable against the IOKit/CF/libSystem dylibs, gated by
# bind_target_audit.sh. No entitlement: the user client checks for root.
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
    [ -f "$S/$f.dylib" ] || { echo "error: $S/$f.dylib not found -- build it first" >&2; exit 1; }
done
[ -d "$OUT/iokitlib_headers" ] || { echo "error: $OUT/iokitlib_headers not found -- run build_libiokituser_dylib.sh first" >&2; exit 1; }

CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -O1 -Wall -Wno-nullability-completeness -DPRIVATE=1
    -Werror=implicit-function-declaration
    -I "../../iokit/BCM43430Bluetooth"
    -I "$OUT/iokitlib_headers" -I "$OUT/compat_include" -I "$OUT/cf_headers"
    -I "$T/xnu-7195/iokit" -I "$T/xnu-7195/libkern" -idirafter "$T/xnu-7195/osfmk")
"$CLANG" "${CFLAGS[@]}" bt.c -o "$OUT/bt.o"

M="$OUT/bt.macho"
rm -f "$M"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o "$M" -DCRT_NO_PTHREAD_BOOTSTRAP "$T/Csu/start.s" "$T/Csu/crt.c" "$OUT/bt.o" \
    "$S/libiokituser.dylib" "$S/libCoreFoundation.dylib" "$S/libsystem_kernel.dylib" \
    "$S/libsystem_c.dylib" "$S/libsystem_pthread.dylib" "$S/libsystem_malloc.dylib" \
    "$S/libsystem_platform.dylib" "$S/libdyld.dylib" "$S/libSystem.B.dylib"

otool -l "$M" | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || { echo "error: no LC_LOAD_DYLINKER" >&2; exit 1; }
otool -l "$M" | grep -q "cmd LC_MAIN" || { echo "error: no LC_MAIN" >&2; exit 1; }
./bind_target_audit.sh "$M" || { echo "error: $M has an unresolvable two-level bind" >&2; exit 1; }
codesign -s - -f "$M" 2>&1 | grep -v "replacing existing signature" || true
echo "wrote $M"
