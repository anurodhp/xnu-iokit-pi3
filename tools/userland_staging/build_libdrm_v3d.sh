#!/bin/bash
# Builds libc_build/system/libdrm.dylib (install name
# /usr/X11/lib/libdrm.2.dylib): the libdrm-compatible shim in v3d_drm/ that
# lets Mesa's vc4 gallium driver reach the BCM2837V3D kext (DARW-296). See
# v3d_drm/v3d_drm.h for what it does and does not provide. The host test
# (tools/v3d_validate_test/run.sh) covers the transport-independent core
# against a mock kernel; this script builds the real IOKit transport.
#
# Same link recipe and hard gates as the other /usr/X11/lib dylibs
# (x11_common.sh): real owner of each symbol listed before libSystem.B,
# bind_target_audit.sh on the result.
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

x11_require_sys_dylibs
for d in libiokituser libCoreFoundation; do
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- build it first" >&2; exit 1; }
done
[ -d "$OUT/iokitlib_headers" ] || { echo "error: $OUT/iokitlib_headers not found -- run build_libiokituser_dylib.sh first" >&2; exit 1; }

SRC=v3d_drm
OBJ="$X11_ROOT/obj/libdrm_v3d"
rm -rf "$OBJ"
mkdir -p "$OBJ"
EXTRA=(-I "$SRC" -I "$SRC/include" -I "$TP/mesa/include" -I ../../iokit/BCM2837V3D -DPRIVATE=1
    -I "$OUT/iokitlib_headers" -I "$OUT/compat_include" -I "$OUT/cf_headers"
    -I "$TP/xnu-7195/iokit" -I "$TP/xnu-7195/libkern" -idirafter "$TP/xnu-7195/osfmk")
x11_compile "$OBJ" "$SRC/v3d_drm.c" "${EXTRA[@]}"
x11_compile "$OBJ" "$SRC/v3d_drm_iokit.c" "${EXTRA[@]}"
x11_compile_report libdrm_v3d

# Public header for consumers (Mesa's vc4 driver, test tools).
mkdir -p "$X11_INC/v3d"
cp "$SRC/v3d_drm.h" ../../iokit/BCM2837V3D/BCM2837V3DUAPI.h "$X11_INC/v3d/"

# libiokituser pulls CoreFoundation in itself; both are listed so their
# symbols bind to their real owners.
x11_link_dylib libdrm.dylib libdrm 2:4:0 "$OBJ" \
    "$OUT/system/libiokituser.dylib" "$OUT/system/libCoreFoundation.dylib"

nm -gU "$OUT/system/libdrm.dylib" | grep -q " _drmIoctl$" || { echo "error: libdrm.dylib does not export drmIoctl" >&2; exit 1; }
nm -gU "$OUT/system/libdrm.dylib" | grep -q " _v3d_drm_open$" || { echo "error: libdrm.dylib does not export v3d_drm_open" >&2; exit 1; }
echo "libdrm.dylib: exports checked"
