#!/bin/bash
# Builds libc_build/system/libXtst.dylib (install name
# /usr/X11/lib/libXtst.6.dylib, -version-number 6:1:0) from real, unmodified
# libXtst-1.2.5 (third_party/libXtst). DAR-387: xdpyinfo's configure.ac:41
# requires it.
# Toolchain, layout and helper functions: x11_common.sh.
#
# Sources: src/Makefile.am's libXtst_la_SOURCES (x11_am_sources);
# AM_CPPFLAGS -I include, include/X11/extensions.
# config: x11_config/libXtst/config.h.
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

SRC="$TP/libXtst"
x11_require_source "$SRC/src/Makefile.am"
grep -q '\[libXtst\], \[1.2.5\]' "$SRC/configure.ac" || { echo "error: libXtst is not 1.2.5 -- recheck this script" >&2; exit 1; }
x11_require_staged X11/extensions/XInput.h build_libXi.sh
for d in libXi libXext libX11; do
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- run build_$d.sh first" >&2; exit 1; }
done
x11_require_sys_dylibs

OBJ="$X11_ROOT/obj/libXtst"
rm -rf "$OBJ"
mkdir -p "$OBJ"
for f in $(x11_am_sources "$SRC/src/Makefile.am" libXtst_la_SOURCES); do
    x11_compile "$OBJ" "$SRC/src/$f" -I "$X11_CONFIG_DIR/libXtst" \
        -I "$SRC/include" -I "$SRC/include/X11/extensions"
done
x11_compile_report libXtst
x11_stage_headers X11/extensions "$SRC/include/X11/extensions/XTest.h" "$SRC/include/X11/extensions/record.h"
x11_link_dylib libXtst.dylib libXtst 6:1:0 "$OBJ" \
    "$OUT/system/libXi.dylib" "$OUT/system/libXext.dylib" "$OUT/system/libX11.dylib"
