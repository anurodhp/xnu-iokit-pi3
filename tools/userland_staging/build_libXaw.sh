#!/bin/bash
# Builds libc_build/system/libXaw.dylib (install name
# /usr/X11/lib/libXaw.7.dylib) from real, unmodified libXaw-1.0.16
# (third_party/libXaw, freedesktop xorg/lib/libxaw). Only Xaw7 is built
# (what xdm's PKG_CHECK_MODULES(XDM_TOOLKIT, xaw7) asks for); Xaw6
# (--disable-xaw6) is skipped. Same layout and helpers as build_libXmu.sh:
# x11_common.sh's header.
#
# Sources: src/Makefile.am's COMMON_SOURCES plus Tip.c (libXaw7_la_SOURCES),
# read with x11_am_sources; libXaw7_la_CFLAGS adds -DXAW7 and AM_CPPFLAGS
# -DPROJECT_ROOT="$(prefix)" (Pixmap.c:654 default is /usr/X11R6, which
# this tree spells /usr/X11). config: x11_config/libXaw/config.h.
# Prerequisites (read by gen_build_order.py): run build_libXpm.sh, build_libXmu.sh, build_libXt.sh, build_libXext.sh, build_libX11.sh first.
# Links libXmu, libXt, libXext, libX11 and libXpm (xaw7.pc Requires: xproto
# x11 xext xextproto xt xmu xpm, configure.ac PKG_CHECK_MODULES(XAW7 ...)).
# -version-info 7:0:0 -> x11_link_dylib -version-number 7:0:0 (libtool
# darwin: current 7, age 0, install-name major 7).
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

SRC="$TP/libXaw"
x11_require_source "$SRC/src/Makefile.am"
grep -q '\[libXaw\], \[1.0.16\]' "$SRC/configure.ac" || { echo "error: $SRC is not libXaw 1.0.16 -- recheck this script" >&2; exit 1; }
if [ ! -f "$X11_INC/X11/Xmd.h" ]; then
    echo "error: $X11_INC/X11/Xmd.h not found -- run build_xorg_headers.sh first" >&2
    exit 1
fi
for pair in libXpm:build_libXpm.sh libXmu:build_libXmu.sh libXt:build_libXt.sh \
            libXext:build_libXext.sh libX11:build_libX11.sh; do
    d="${pair%%:*}"
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- run ${pair##*:} first" >&2; exit 1; }
done
x11_require_staged X11/xpm.h build_libXpm.sh
x11_require_staged X11/Xmu/Xmu.h build_libXmu.sh
for d in libsystem_kernel libsystem_c libsystem_pthread libsystem_malloc libsystem_platform libdyld; do
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2; exit 1; }
done
x11_require_sys_dylibs

# include/Makefile.am: xawinclude_HEADERS -> $(includedir)/X11/Xaw
# (read from the list, so Template.h/TemplateP.h, which it omits, are not
# staged).
mkdir -p "$X11_INC/X11/Xaw"
for h in $(grep -o 'X11/Xaw/[A-Za-z0-9_]*\.h' "$SRC/include/Makefile.am"); do
    cp "$SRC/include/$h" "$X11_INC/X11/Xaw/"
done

OBJ="$X11_ROOT/obj/libXaw"
rm -rf "$OBJ"
mkdir -p "$OBJ"
for f in $(x11_am_sources "$SRC/src/Makefile.am" COMMON_SOURCES) Tip.c; do
    x11_compile "$OBJ" "$SRC/src/$f" -I "$X11_CONFIG_DIR/libXaw" -I "$SRC/include" -I "$SRC/src" \
        "-DPROJECT_ROOT=\"$X11_PREFIX\"" -DXAW7
done
x11_compile_report libXaw
x11_link_dylib libXaw.dylib libXaw 7:0:0 "$OBJ" "$OUT/system/libXmu.dylib" "$OUT/system/libXt.dylib" \
    "$OUT/system/libXext.dylib" "$OUT/system/libXpm.dylib" "$OUT/system/libX11.dylib"
