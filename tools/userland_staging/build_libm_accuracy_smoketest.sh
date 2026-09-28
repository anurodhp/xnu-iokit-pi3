#!/bin/bash
# Builds libc_build/libm_accuracy_smoketest.macho (DAR-447): libsystem_m's
# real FreeBSD msun sin/cos/tan/atan/atan2/log/floor/ceil/round and
# sinf/cosf/atan2f against
# correctly rounded values, see libm_accuracy_smoketest.c. Dynamic, like
# build_ffi_smoketest.sh; libsystem_m.dylib is listed as the owner.
set -euo pipefail
cd "$(dirname "$0")"
OUT="libc_build"
CSU="../../third_party/Csu"
XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
[ -f "$OUT/system/libsystem_m.dylib" ] || { echo "error: $OUT/system/libsystem_m.dylib not found -- run build_libm_dylib.sh first" >&2; exit 1; }
# -fno-builtin: calls must reach libsystem_m, not be constant-folded.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -O0 -Wall -fno-builtin \
    -fno-stack-protector -D_FORTIFY_SOURCE=0 libm_accuracy_smoketest.c -o "$OUT/libm_accuracy_smoketest.o"
M="$OUT/libm_accuracy_smoketest.macho"
rm -f "$M"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" -Wl,-fixup_chains -o "$M" \
    "$CSU/start.s" "$CSU/crt.c" "$OUT/libm_accuracy_smoketest.o" \
    "$OUT/system/libsystem_m.dylib" \
    "$OUT/system/libsystem_kernel.dylib" "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libsystem_malloc.dylib" "$OUT/system/libsystem_platform.dylib" \
    "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib"
otool -l "$M" | grep -q "cmd LC_MAIN" || { echo "error: $M has no LC_MAIN" >&2; exit 1; }
dyld_info -fixups "$M" | grep -q "libsystem_m/_sin" || { echo "error: $M does not bind _sin to libsystem_m" >&2; exit 1; }
./bind_target_audit.sh "$M"
echo "wrote $M"
