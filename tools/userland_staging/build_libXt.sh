#!/bin/bash
# Builds libc_build/system/libXt.dylib (install name
# /usr/X11/lib/libXt.6.dylib, -version-number 6:0:0) from real, unmodified
# libXt-1.3.1 (third_party/libXt, freedesktop xorg/lib/libxt). DAR-383, X11GS 2.
# Toolchain, layout and helper functions: x11_common.sh's header.
#
# Sources: src/Makefile.am's libXt_la_SOURCES, read with x11_am_sources.
# config: x11_config/libXt/config.h.
# Plus nodist StringDefs.c, generated the way src/Makefile.am does it:
# util/makestrs.c built with the HOST compiler (CC_FOR_BUILD) and run as
# `makestrs -i <top_srcdir> < util/string.list > StringDefs.c`
# (STRINGSABIOPTIONS is empty on Darwin, configure.ac:176-191); it also
# writes the installed StringDefs.h and Shell.h. src/Makefile.am AM_CFLAGS:
# -DXFILESEARCHPATHDEFAULT (configure.ac:156-166 default, with
# sysconfdir/datadir/libdir for --prefix=/usr/X11), -DERRORDB, and
# -DLIBXT_COMPILATION. Links libSM, libICE, libX11 (xt.pc Requires).
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

SRC="$TP/libXt"
x11_require_source "$SRC/src/Makefile.am"
if [ ! -f "$X11_INC/X11/Xmd.h" ]; then
    echo "error: $X11_INC/X11/Xmd.h not found -- run build_xorg_headers.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libX11.dylib" ]; then
    echo "error: $OUT/system/libX11.dylib not found -- run build_libX11.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libSM.dylib" ]; then
    echo "error: $OUT/system/libSM.dylib not found -- run build_libSM.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libICE.dylib" ]; then
    echo "error: $OUT/system/libICE.dylib not found -- run build_libICE.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libinfo.dylib" ]; then
    echo "error: $OUT/system/libinfo.dylib not found -- run build_libinfo.sh first" >&2
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

GEN="$X11_ROOT/gen/libXt"
rm -rf "$GEN"
mkdir -p "$GEN"
/usr/bin/cc -I "$X11_INC" -o "$GEN/makestrs" "$SRC/util/makestrs.c"
(cd "$GEN" && ./makestrs -i "$OLDPWD/$SRC" < "$OLDPWD/$SRC/util/string.list" > StringDefs.c)
for f in StringDefs.c StringDefs.h Shell.h; do
    [ -s "$GEN/$f" ] || { echo "error: makestrs did not produce $f" >&2; exit 1; }
done
x11_stage_headers X11 "$SRC"/include/X11/*.h "$GEN/StringDefs.h" "$GEN/Shell.h"

E=/usr/X11/etc D=/usr/X11/share L=/usr/X11/lib
SEARCH="$E/X11/%L/%T/%N%C%S:$E/X11/%l/%T/%N%C%S:$E/X11/%T/%N%C%S:$E/X11/%L/%T/%N%S:$E/X11/%l/%T/%N%S:$E/X11/%T/%N%S"
SEARCH="$SEARCH:$D/X11/%L/%T/%N%C%S:$D/X11/%l/%T/%N%C%S:$D/X11/%T/%N%C%S:$D/X11/%L/%T/%N%S:$D/X11/%l/%T/%N%S:$D/X11/%T/%N%S"
SEARCH="$SEARCH:$L/X11/%L/%T/%N%C%S:$L/X11/%l/%T/%N%C%S:$L/X11/%T/%N%C%S:$L/X11/%L/%T/%N%S:$L/X11/%l/%T/%N%S:$L/X11/%T/%N%S"

OBJ="$X11_ROOT/obj/libXt"
rm -rf "$OBJ"
mkdir -p "$OBJ"
for f in $(x11_am_sources "$SRC/src/Makefile.am" libXt_la_SOURCES); do
    x11_compile "$OBJ" "$SRC/src/$f" -I "$X11_CONFIG_DIR/libXt" \
        -I "$SRC/include" -I "$SRC/include/X11" -I "$X11_INC/X11" \
        "-DXFILESEARCHPATHDEFAULT=\"$SEARCH\"" "-DERRORDB=\"$D/X11/XtErrorDB\"" -DLIBXT_COMPILATION
done
x11_compile "$OBJ" "$GEN/StringDefs.c" -I "$X11_CONFIG_DIR/libXt" \
    -I "$SRC/include" -I "$SRC/include/X11" -I "$X11_INC/X11" -DLIBXT_COMPILATION
x11_compile_report libXt
x11_link_dylib libXt.dylib libXt 6:0:0 "$OBJ" "$OUT/system/libinfo.dylib" "$OUT/system/libresolv.dylib" \
    "$OUT/system/libSM.dylib" "$OUT/system/libICE.dylib" "$OUT/system/libX11.dylib"
