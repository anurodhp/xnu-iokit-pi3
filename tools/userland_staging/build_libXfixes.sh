#!/bin/bash
# Builds libc_build/system/libXfixes.dylib (install name
# /usr/X11/lib/libXfixes.3.dylib, -version-number 3:1:0) from real, unmodified
# libXfixes-6.0.2 (third_party/libXfixes, freedesktop xorg/lib/libxfixes). DAR-383, X11GS 2.
# Toolchain, layout and helper functions: x11_common.sh's header.
#
# Sources: src/Makefile.am's libXfixes_la_SOURCES, read with x11_am_sources.
# config: x11_config/libXfixes/config.h.
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

SRC="$TP/libXfixes"
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

OBJ="$X11_ROOT/obj/libXfixes"
rm -rf "$OBJ"
mkdir -p "$OBJ"
x11_stage_headers X11/extensions "$SRC/include/X11/extensions/Xfixes.h"
for f in $(x11_am_sources "$SRC/src/Makefile.am" libXfixes_la_SOURCES); do
    x11_compile "$OBJ" "$SRC/src/$f" -I "$X11_CONFIG_DIR/libXfixes" \
        -I "$SRC/include/X11/extensions"
done
x11_compile_report libXfixes
x11_link_dylib libXfixes.dylib libXfixes 3:1:0 "$OBJ" "$OUT/system/libX11.dylib"
