#!/bin/bash
# Builds /usr/lib/libusbhid.dylib: NetBSD's HID report-descriptor parser
# (lib/libusbhid from our fork anurodhp/netbsd-src), which btdevctl(8) uses
# to describe a Bluetooth HID device's descriptor (usr.sbin/btdevctl/
# print.c). Usage names come from /usr/share/misc/usb_hid_usages
# (lib/libusbhid/usb_hid_usages, installed by inject_into_sd_image.sh).
# docs/bluetooth-stack-plan.md S5. Needs build_libbluetooth.sh's staged
# headers (libc_build/netbt_include: dev/usb, dev/hid).
set -euo pipefail
cd "$(dirname "$0")"

OUT="libc_build"
T="../../third_party"
NB="$T/netbsd-src"
COMPAT="../../compat/netbt-user"
XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
NEWLD_BINDIR="$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain/usr/bin"
S="$OUT/system"
INC="$OUT/netbt_include"

[ -d "$NB/lib/libusbhid" ] || { echo "error: $NB/lib/libusbhid not found -- run ./setup_third_party.sh" >&2; exit 1; }
[ -d "$INC/dev/usb" ] || { echo "error: run build_libbluetooth.sh first (staged headers)" >&2; exit 1; }

cp "$NB/lib/libusbhid/usbhid.h" "$INC/usbhid.h"
OBJ="$OUT/libusbhid_obj"
rm -rf "$OBJ"
mkdir -p "$OBJ"
CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -O1 -Wno-nullability-completeness
    -D_FORTIFY_SOURCE=0 -Werror=implicit-function-declaration -Werror=int-conversion
    -include "$COMPAT/netbt_user.h" -I "$INC" -I "$NB/lib/libusbhid")
for s in descr parse usage data; do       # lib/libusbhid/Makefile SRCS
    "$CLANG" "${CFLAGS[@]}" "$NB/lib/libusbhid/$s.c" -o "$OBJ/$s.o"
done
# libc gaps (not exported by this port's libsystem_c): err(3) from
# compat/netbt-user/err_shim.c, fmtcheck(3) from Apple Libc, unmodified.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -O1 "$COMPAT/err_shim.c" -o "$OBJ/err_shim.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -O1 '-D__FBSDID(s)=' \
    '-D__weak_reference(a,b)=' -D__fmtcheck=fmtcheck \
    "$T/Libc/gen/FreeBSD/fmtcheck.c" -o "$OBJ/fmtcheck.o"
D="$S/libusbhid.dylib"
rm -f "$D"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/libusbhid.dylib \
    -Wl,-not_for_dyld_shared_cache -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o "$D" "$OBJ"/*.o \
    "$S/libsystem_c.dylib" "$S/libsystem_malloc.dylib" "$S/libsystem_kernel.dylib" \
    "$S/libsystem_platform.dylib" "$S/libsystem_pthread.dylib" "$S/libdyld.dylib" "$S/libSystem.B.dylib"
./bind_target_audit.sh "$D" || { echo "error: $D has an unresolvable two-level bind" >&2; exit 1; }
codesign -s - -f "$D" 2>&1 | grep -v "replacing existing signature" || true
echo "wrote $D"
