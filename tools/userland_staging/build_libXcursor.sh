#!/bin/bash
# Builds libc_build/system/libXcursor.dylib (install name
# /usr/X11/lib/libXcursor.1.dylib, -version-number 1:0:2) from real, unmodified
# libXcursor-1.2.3 (third_party/libXcursor, freedesktop xorg/lib/libxcursor). DAR-383, X11GS 2.
# Toolchain, layout and helper functions: x11_common.sh's header.
#
# Sources: src/Makefile.am's libXcursor_la_SOURCES, read with x11_am_sources.
# config: x11_config/libXcursor/config.h.
# include/X11/Xcursor/Xcursor.h is an AC_CONFIG_HEADERS output: its
# #undef XCURSOR_LIB_{MAJOR,MINOR,REVISION} lines become #defines of
# PACKAGE_VERSION's parts (configure.ac:55-63), done here with sed.
# ICONDIR / XCURSORPATH: configure.ac:66-83's defaults for
# --prefix=/usr/X11 (datadir /usr/X11/share).
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

SRC="$TP/libXcursor"
x11_require_source "$SRC/src/Makefile.am"
if [ ! -f "$X11_INC/X11/Xmd.h" ]; then
    echo "error: $X11_INC/X11/Xmd.h not found -- run build_xorg_headers.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libX11.dylib" ]; then
    echo "error: $OUT/system/libX11.dylib not found -- run build_libX11.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libXrender.dylib" ]; then
    echo "error: $OUT/system/libXrender.dylib not found -- run build_libXrender.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libXfixes.dylib" ]; then
    echo "error: $OUT/system/libXfixes.dylib not found -- run build_libXfixes.sh first" >&2
    exit 1
fi
for d in libsystem_kernel libsystem_c libsystem_pthread libsystem_malloc libsystem_platform libdyld; do
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2; exit 1; }
done
x11_require_sys_dylibs

OBJ="$X11_ROOT/obj/libXcursor"
rm -rf "$OBJ"
mkdir -p "$OBJ" "$X11_INC/X11/Xcursor"
sed -e 's/^#undef XCURSOR_LIB_MAJOR$/#define XCURSOR_LIB_MAJOR 1/' \
    -e 's/^#undef XCURSOR_LIB_MINOR$/#define XCURSOR_LIB_MINOR 2/' \
    -e 's/^#undef XCURSOR_LIB_REVISION$/#define XCURSOR_LIB_REVISION 3/' \
    "$SRC/include/X11/Xcursor/Xcursor.h.in" > "$X11_INC/X11/Xcursor/Xcursor.h"
grep -q '^#define XCURSOR_LIB_REVISION 3$' "$X11_INC/X11/Xcursor/Xcursor.h" \
    || { echo "error: Xcursor.h.in substitution failed" >&2; exit 1; }
ICONDIR="$X11_PREFIX/share/icons"
XCURSORPATH="~/.local/share/icons:~/.icons:$X11_PREFIX/share/icons:$X11_PREFIX/share/pixmaps"
for f in $(x11_am_sources "$SRC/src/Makefile.am" libXcursor_la_SOURCES); do
    x11_compile "$OBJ" "$SRC/src/$f" -I "$X11_CONFIG_DIR/libXcursor" \
        -I "$X11_INC/X11/Xcursor" "-DICONDIR=\"$ICONDIR\"" "-DXCURSORPATH=\"$XCURSORPATH\""
done
x11_compile_report libXcursor
x11_link_dylib libXcursor.dylib libXcursor 1:0:2 "$OBJ" \
    "$OUT/system/libXfixes.dylib" "$OUT/system/libXrender.dylib" "$OUT/system/libX11.dylib"
