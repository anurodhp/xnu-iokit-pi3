#!/bin/bash
# Builds libc_build/system/libXext.dylib (install name
# /usr/X11/lib/libXext.6.dylib, -version-number 6:4:0) from real, unmodified
# libXext-1.3.7 (third_party/libXext, freedesktop xorg/lib/libxext). DAR-383, X11GS 2.
# Toolchain, layout and helper functions: x11_common.sh's header.
#
# Sources: src/Makefile.am's libXext_la_SOURCES, read with x11_am_sources.
# config: x11_config/libXext/config.h.
# XEXT_SOREV is 6:4:0 on everything but openbsd/solaris (configure.ac:35-40).
# Plus src/reallocarray.c (AC_REPLACE_FUNCS, see config.h).
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

SRC="$TP/libXext"
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

OBJ="$X11_ROOT/obj/libXext"
rm -rf "$OBJ"
mkdir -p "$OBJ"
x11_stage_headers X11/extensions "$SRC"/include/X11/extensions/*.h
for f in $(x11_am_sources "$SRC/src/Makefile.am" libXext_la_SOURCES) reallocarray.c; do
    x11_compile "$OBJ" "$SRC/src/$f" -I "$X11_CONFIG_DIR/libXext" \
        -I "$SRC/include" -I "$SRC/include/X11/extensions"
done
x11_compile_report libXext
x11_link_dylib libXext.dylib libXext 6:4:0 "$OBJ" "$OUT/system/libX11.dylib"
