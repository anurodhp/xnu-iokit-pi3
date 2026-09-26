#!/bin/bash
# Builds libc_build/system/libXrender.dylib (install name
# /usr/X11/lib/libXrender.1.dylib, -version-number 1:3:0) from real, unmodified
# libXrender-0.9.12 (third_party/libXrender, freedesktop xorg/lib/libxrender). DAR-383, X11GS 2.
# Toolchain, layout and helper functions: x11_common.sh's header.
#
# Sources: src/Makefile.am's libXrender_la_SOURCES, read with x11_am_sources.
# config: x11_config/libXrender/config.h.
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

SRC="$TP/libXrender"
x11_require_source "$SRC/src/Makefile.am"
if [ ! -f "$X11_INC/X11/Xmd.h" ]; then
    echo "error: $X11_INC/X11/Xmd.h not found -- run build_xorg_headers.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libX11.dylib" ]; then
    echo "error: $OUT/system/libX11.dylib not found -- run build_libX11.sh first" >&2
    exit 1
fi
for d in libsystem_kernel libsystem_c libsystem_pthread libsystem_malloc libsystem_platform libdyld; do
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2; exit 1; }
done
x11_require_sys_dylibs

OBJ="$X11_ROOT/obj/libXrender"
rm -rf "$OBJ"
mkdir -p "$OBJ"
x11_stage_headers X11/extensions "$SRC/include/X11/extensions/Xrender.h"
for f in $(x11_am_sources "$SRC/src/Makefile.am" libXrender_la_SOURCES); do
    x11_compile "$OBJ" "$SRC/src/$f" -I "$X11_CONFIG_DIR/libXrender" \
        -I "$SRC" -I "$SRC/include/X11/extensions"
done
x11_compile_report libXrender
x11_link_dylib libXrender.dylib libXrender 1:3:0 "$OBJ" "$OUT/system/libX11.dylib"
