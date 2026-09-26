#!/bin/bash
# Builds libc_build/system/libXmu.dylib (install name
# /usr/X11/lib/libXmu.6.dylib, -version-number 6:2:0) from real, unmodified
# libXmu-1.3.1 (third_party/libXmu, freedesktop xorg/lib/libxmu). DAR-383, X11GS 2.
# Toolchain, layout and helper functions: x11_common.sh's header.
#
# Sources: src/Makefile.am's libXmu_la_SOURCES, read with x11_am_sources.
# config: x11_config/libXmu/config.h.
# Also builds libXmuu (/usr/X11/lib/libXmuu.1.dylib, libXmuu_la_SOURCES,
# -version-number 1:0:0), the Xt-free subset xdpyinfo-class clients use.
# src/Makefile.am AM_CPPFLAGS: -DBITMAPDIR=<includedir>/X11/bitmaps and
# -D_CONST_X_STRING. Plus src/reallocarray.c (AC_REPLACE_FUNCS, see
# config.h). libXmu links libXt, libXext and libX11; libXmuu only libX11;
# both also libresolv (gethostname, CvtStdSel.c/ClientWin-side host lookups).
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

SRC="$TP/libXmu"
x11_require_source "$SRC/src/Makefile.am"
if [ ! -f "$X11_INC/X11/Xmd.h" ]; then
    echo "error: $X11_INC/X11/Xmd.h not found -- run build_xorg_headers.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libXt.dylib" ]; then
    echo "error: $OUT/system/libXt.dylib not found -- run build_libXt.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libXext.dylib" ]; then
    echo "error: $OUT/system/libXext.dylib not found -- run build_libXext.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libX11.dylib" ]; then
    echo "error: $OUT/system/libX11.dylib not found -- run build_libX11.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libresolv.dylib" ]; then
    echo "error: $OUT/system/libresolv.dylib not found -- run build_libresolv_dylib.sh first" >&2
    exit 1
fi
for d in libsystem_kernel libsystem_c libsystem_pthread libsystem_malloc libsystem_platform libdyld; do
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2; exit 1; }
done
x11_require_sys_dylibs

FLAGS=(-I "$X11_CONFIG_DIR/libXmu" -I "$SRC/include" -I "$SRC/include/X11/Xmu"
    "-DBITMAPDIR=\"$X11_PREFIX/include/X11/bitmaps\"" -D_CONST_X_STRING)
x11_stage_headers X11/Xmu "$SRC"/include/X11/Xmu/*.h

OBJ="$X11_ROOT/obj/libXmu"
rm -rf "$OBJ"
mkdir -p "$OBJ"
# libXmu_la_SOURCES starts with $(libXmuu_la_SOURCES); x11_am_sources
# reads only literal .c entries, so both lists are read and concatenated.
for f in $(x11_am_sources "$SRC/src/Makefile.am" libXmuu_la_SOURCES) \
         $(x11_am_sources "$SRC/src/Makefile.am" libXmu_la_SOURCES) reallocarray.c; do
    x11_compile "$OBJ" "$SRC/src/$f" "${FLAGS[@]}"
done
x11_compile_report libXmu
x11_link_dylib libXmu.dylib libXmu 6:2:0 "$OBJ" "$OUT/system/libresolv.dylib" \
    "$OUT/system/libXt.dylib" "$OUT/system/libXext.dylib" "$OUT/system/libX11.dylib"

X11_OK=0 X11_FAIL=0
UOBJ="$X11_ROOT/obj/libXmuu"
rm -rf "$UOBJ"
mkdir -p "$UOBJ"
for f in $(x11_am_sources "$SRC/src/Makefile.am" libXmuu_la_SOURCES); do
    x11_compile "$UOBJ" "$SRC/src/$f" "${FLAGS[@]}"
done
x11_compile_report libXmuu
x11_link_dylib libXmuu.dylib libXmuu 1:0:0 "$UOBJ" "$OUT/system/libX11.dylib" "$OUT/system/libresolv.dylib"
