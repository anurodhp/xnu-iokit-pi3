#!/bin/bash
# Builds hid_userdevice_test.macho (docs/bluetooth-stack-plan.md, step H0):
# hid_userdevice_test.c against this port's IOKitLib (libiokituser.dylib) and
# CoreFoundation, ad-hoc signed with hid_userdevice_test.entitlements
# (com.apple.hid.manager.user-access-device), which
# IOHIDResourceDeviceUserClient requires (IOHIDResourceUserClient.cpp:101-131).
# Link line: build_hid_event_smoketest.sh's.
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


CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -O0 -Wno-nullability-completeness -DPRIVATE=1
    -Werror=implicit-function-declaration
    -I "$OUT/iokitlib_headers" -I "$OUT/compat_include" -I "$OUT/cf_headers"
    -I "$T/xnu-7195/iokit" -I "$T/xnu-7195/libkern" -idirafter "$T/xnu-7195/osfmk")
"$CLANG" "${CFLAGS[@]}" hid_userdevice_test.c -o "$OUT/hid_userdevice_test.o"

M="$OUT/hid_userdevice_test.macho"
rm -f "$M"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o "$M" -DCRT_NO_PTHREAD_BOOTSTRAP "$T/Csu/start.s" "$T/Csu/crt.c" "$OUT/hid_userdevice_test.o" \
    "$S/libiokituser.dylib" "$S/libCoreFoundation.dylib" "$S/libsystem_kernel.dylib" \
    "$S/libsystem_c.dylib" "$S/libsystem_pthread.dylib" "$S/libsystem_malloc.dylib" \
    "$S/libsystem_platform.dylib" "$S/libdyld.dylib" "$S/libSystem.B.dylib"
otool -l "$M" | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || { echo "error: no LC_LOAD_DYLINKER" >&2; exit 1; }
otool -l "$M" | grep -q "cmd LC_MAIN" || { echo "error: no LC_MAIN" >&2; exit 1; }
./bind_target_audit.sh "$M" || { echo "error: $M has an unresolvable two-level bind" >&2; exit 1; }
# The user client refuses any task without this entitlement
# (IOHIDEventServiceUserClient.cpp:242-253); the kernel reads it from the
# code-signature blob (IOUserClient.cpp:1481-1484, cs_entitlements_blob_get).
codesign -s - -f --entitlements hid_userdevice_test.entitlements "$M" 2>&1 | grep -v "replacing existing signature" || true
codesign -d --entitlements - "$M" 2>/dev/null | grep -q "com.apple.hid.manager.user-access-device" || {
    echo "error: $M is not signed with the HID entitlement" >&2; exit 1; }
echo "wrote $M"

