#!/bin/bash
# Builds libc_build/system/libXi.dylib (install name /usr/X11/lib/libXi.6.dylib,
# -version-number 6:1:0) from real, unmodified libXi-1.8.2 (third_party/libXi).
# DAR-387: libXtst needs it, and xdpyinfo needs libXtst.
# Toolchain, layout and helper functions: x11_common.sh.
#
# Sources: src/Makefile.am's libXi_la_SOURCES plus the XI2_sources list it
# pulls in through $(XI2_sources), both read with x11_am_sources.
# AM_CPPFLAGS: -I include, include/X11, include/X11/extensions.
# config: x11_config/libXi/config.h.
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

SRC="$TP/libXi"
x11_require_source "$SRC/src/Makefile.am"
grep -q '\[libXi\], \[1.8.2\]' "$SRC/configure.ac" || { echo "error: libXi is not 1.8.2 -- recheck this script" >&2; exit 1; }
x11_require_staged X11/extensions/XI2proto.h build_xorg_headers.sh
for d in libXext libXfixes libX11; do
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- run build_$d.sh first" >&2; exit 1; }
done
x11_require_sys_dylibs

OBJ="$X11_ROOT/obj/libXi"
rm -rf "$OBJ"
mkdir -p "$OBJ"
SRCS="$(x11_am_sources "$SRC/src/Makefile.am" libXi_la_SOURCES) $(x11_am_sources "$SRC/src/Makefile.am" XI2_sources)"
for f in $SRCS; do
    x11_compile "$OBJ" "$SRC/src/$f" -I "$X11_CONFIG_DIR/libXi" \
        -I "$SRC/include" -I "$SRC/include/X11" -I "$SRC/include/X11/extensions"
done
x11_compile_report libXi
x11_stage_headers X11/extensions "$SRC/include/X11/extensions/XInput.h" "$SRC/include/X11/extensions/XInput2.h"
x11_link_dylib libXi.dylib libXi 6:1:0 "$OBJ" \
    "$OUT/system/libXext.dylib" "$OUT/system/libXfixes.dylib" "$OUT/system/libX11.dylib"
