#!/bin/bash
# Builds libc_build/system/libxcvt.dylib (install name
# /usr/X11/lib/libxcvt.0.dylib) from real, unmodified libxcvt-0.1.3
# (third_party/libxcvt). DAR-389: the Xorg server's modeline library;
# xserver's meson.build requires it when -Dxorg=true.
# Follows libxcvt's own meson build: lib/meson.build builds libxcvt.c into
# shared_library('xcvt', version 0.1.3, darwin_versions 1.0.0/1.0.0), which
# meson names libxcvt.0.dylib; meson.build:10 links libm when present.
# Headers libxcvt/libxcvt.h and libxcvt_mode.h go to $X11_INC/libxcvt.
set -e
cd "$(dirname "$0")"
. ./fontstack_common.sh

SRC="$TP/libxcvt"
x11_require_source "$SRC/lib/libxcvt.c"
grep -q "version: '0.1.3'" "$SRC/meson.build" || { echo "error: libxcvt is not 0.1.3 -- recheck this script" >&2; exit 1; }
[ -f "$OUT/system/libsystem_m.dylib" ] || { echo "error: $OUT/system/libsystem_m.dylib not found -- run build_libm_dylib.sh first" >&2; exit 1; }
x11_require_sys_dylibs

OBJ="$X11_ROOT/obj/libxcvt"
rm -rf "$OBJ"
mkdir -p "$OBJ"
x11_compile "$OBJ" "$SRC/lib/libxcvt.c" -UHAVE_CONFIG_H -I "$SRC/include"
x11_compile_report libxcvt
mkdir -p "$X11_INC/libxcvt"
cp "$SRC/include/libxcvt/libxcvt.h" "$SRC/include/libxcvt/libxcvt_mode.h" "$X11_INC/libxcvt/"
fs_link_dylib libxcvt.dylib "$X11_PREFIX/lib/libxcvt.0.dylib" 1.0.0 1.0.0 "$OBJ" \
    "$OUT/system/libsystem_m.dylib"
