#!/bin/bash
# Builds libc_build/system/libXrandr.dylib (install name
# /usr/X11/lib/libXrandr.2.dylib, -version-number 2:2:0) from real, unmodified
# libXrandr-1.5.5 (third_party/libXrandr, freedesktop xorg/lib/libxrandr). DAR-383, X11GS 2.
# Toolchain, layout and helper functions: x11_common.sh's header.
#
# Sources: src/Makefile.am's libXrandr_la_SOURCES, read with x11_am_sources.
# config: x11_config/libXrandr/config.h.
# Links libXext + libXrender (configure.ac's PKG_CHECK_MODULES(RANDR,
# x11 randrproto xext xextproto xrender renderproto)).
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

SRC="$TP/libXrandr"
x11_require_source "$SRC/src/Makefile.am"
if [ ! -f "$X11_INC/X11/Xmd.h" ]; then
    echo "error: $X11_INC/X11/Xmd.h not found -- run build_xorg_headers.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libX11.dylib" ]; then
    echo "error: $OUT/system/libX11.dylib not found -- run build_libX11.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libXext.dylib" ]; then
    echo "error: $OUT/system/libXext.dylib not found -- run build_libXext.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libXrender.dylib" ]; then
    echo "error: $OUT/system/libXrender.dylib not found -- run build_libXrender.sh first" >&2
    exit 1
fi
for d in libsystem_kernel libsystem_c libsystem_pthread libsystem_malloc libsystem_platform libdyld; do
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2; exit 1; }
done
x11_require_sys_dylibs

OBJ="$X11_ROOT/obj/libXrandr"
rm -rf "$OBJ"
mkdir -p "$OBJ"
x11_stage_headers X11/extensions "$SRC/include/X11/extensions/Xrandr.h"
for f in $(x11_am_sources "$SRC/src/Makefile.am" libXrandr_la_SOURCES); do
    x11_compile "$OBJ" "$SRC/src/$f" -I "$X11_CONFIG_DIR/libXrandr" \
        -I "$SRC/include/X11/extensions"
done
x11_compile_report libXrandr
x11_link_dylib libXrandr.dylib libXrandr 2:2:0 "$OBJ" \
    "$OUT/system/libXrender.dylib" "$OUT/system/libXext.dylib" "$OUT/system/libX11.dylib"
