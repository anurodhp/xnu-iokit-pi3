#!/bin/bash
# Builds /usr/lib/libbluetooth.dylib: NetBSD's lib/libbluetooth (bt_aton/
# bt_ntoa, bt_dev* HCI device access, the SDP client library) from our fork
# anurodhp/netbsd-src (iokit-port; third_party/netbsd-src), for the netbt
# userland (docs/bluetooth-stack-plan.md S3).
#
# Also stages the headers the netbt userland compiles against into
# libc_build/netbt_include/: netbt's own (netbt/bluetooth.h, hci.h, l2cap.h,
# rfcomm.h, sco.h -- the kernel's, from the same fork, so ioctl numbers and
# structures match), libbluetooth's (bluetooth.h, sdp.h), and the NetBSD
# headers Darwin lacks from compat/netbt-user/ (sys/endian.h, sys/atomic.h,
# uuid.h). h_errno (bt_gethostbyname, bluetooth.c) is libresolv's. Every netbt userland object is compiled with
# -include compat/netbt-user/netbt_user.h (AF_BLUETOOTH and NetBSD cdefs).
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

[ -d "$NB/lib/libbluetooth" ] || { echo "error: $NB/lib/libbluetooth not found -- run ./setup_third_party.sh" >&2; exit 1; }
for f in libresolv libsystem_kernel libsystem_c libsystem_malloc libsystem_platform libsystem_pthread libdyld libSystem.B; do
    [ -f "$S/$f.dylib" ] || { echo "error: $S/$f.dylib not found -- build it first" >&2; exit 1; }
done

INC="$OUT/netbt_include"
rm -rf "$INC"
mkdir -p "$INC/netbt" "$INC/sys" "$INC/dev/bluetooth" "$INC/dev/usb" "$INC/dev/hid"
cp "$NB"/sys/netbt/{bluetooth.h,hci.h,hci_le.h,l2cap.h,rfcomm.h,sco.h} "$INC/netbt/"
# btdevctl/bthidd: the device-cache keys and HID protocol constants
# (dev/bluetooth), HID descriptor constants (dev/usb/usbhid.h, dev/hid/hid.h).
cp "$NB"/sys/dev/bluetooth/{btdev.h,bthidev.h,bthid.h,btsco.h} "$INC/dev/bluetooth/"
cp "$NB"/sys/dev/usb/{usb.h,usbhid.h} "$INC/dev/usb/"
cp "$NB"/sys/dev/hid/hid.h "$INC/dev/hid/"
cp "$NB"/lib/libbluetooth/{bluetooth.h,sdp.h} "$INC/"
cp "$COMPAT"/uuid.h "$INC/"
cp "$COMPAT"/sys/*.h "$INC/sys/"

CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -O1 -Wall -Wno-nullability-completeness
    -Wno-unused-function -Wno-sign-compare
    -Werror=implicit-function-declaration -Werror=int-conversion -Werror=incompatible-pointer-types
    -include "$COMPAT/netbt_user.h" -I "$INC" -I "$NB/lib/libbluetooth")

OBJ="$OUT/libbluetooth_obj"
rm -rf "$OBJ"
mkdir -p "$OBJ"
# lib/libbluetooth/Makefile: SRCS, plus sdp_compat.c (SDP_COMPAT defaults to yes).
for s in bluetooth bt_dev sdp_data sdp_get sdp_match sdp_put sdp_record sdp_set \
         sdp_uuid sdp_service sdp_session sdp_compat; do
    "$CLANG" "${CFLAGS[@]}" "$NB/lib/libbluetooth/$s.c" -o "$OBJ/$s.o"
done

D="$S/libbluetooth.dylib"
rm -f "$D"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/libbluetooth.dylib \
    -Wl,-not_for_dyld_shared_cache -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o "$D" "$OBJ"/*.o \
    "$S/libresolv.dylib" "$S/libsystem_c.dylib" "$S/libsystem_malloc.dylib" "$S/libsystem_kernel.dylib" \
    "$S/libsystem_platform.dylib" "$S/libsystem_pthread.dylib" "$S/libdyld.dylib" "$S/libSystem.B.dylib"
otool -hv "$D" | grep -q " DYLIB " || { echo "error: $D is not a dylib" >&2; exit 1; }
./bind_target_audit.sh "$D" || { echo "error: $D has an unresolvable two-level bind" >&2; exit 1; }
codesign -s - -f "$D" 2>&1 | grep -v "replacing existing signature" || true
echo "wrote $D"
