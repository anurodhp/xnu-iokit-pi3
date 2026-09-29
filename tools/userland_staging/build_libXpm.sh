#!/bin/bash
# Builds libc_build/system/libXpm.dylib (install name
# /usr/X11/lib/libXpm.4.dylib, -version-number 4:11:0 from src/Makefile.am)
# from real, unmodified libXpm-3.5.19 (third_party/libXpm, freedesktop
# xorg/lib/libxpm). Needed by libXaw (src/Pixmap.c includes <X11/xpm.h>
# unconditionally) and by xdm's greeter logo.
# Toolchain, layout and helper functions: x11_common.sh's header.
#
# Sources: src/Makefile.am's libXpm_la_SOURCES (x11_am_sources).
# config: x11_config/libXpm/config.h. AM_CPPFLAGS: -I include/X11/.
# Prerequisites (read by gen_build_order.py): run build_libX11.sh first.
# Links libX11 only (PKG_CHECK_MODULES(XPM, xproto x11), configure.ac:28).
# sxpm/cxpm (the command-line tools) are not built.
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

SRC="$TP/libXpm"
x11_require_source "$SRC/src/Makefile.am"
grep -q '\[libXpm\], \[3.5.19\]' "$SRC/configure.ac" || { echo "error: $SRC is not libXpm 3.5.19 -- recheck this script" >&2; exit 1; }
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

x11_stage_headers X11 "$SRC/include/X11/xpm.h"
OBJ="$X11_ROOT/obj/libXpm"
rm -rf "$OBJ"
mkdir -p "$OBJ"
for f in $(x11_am_sources "$SRC/src/Makefile.am" libXpm_la_SOURCES); do
    x11_compile "$OBJ" "$SRC/src/$f" -I "$X11_CONFIG_DIR/libXpm" -I "$SRC/include/X11" -I "$SRC/src"
done
x11_compile_report libXpm
x11_link_dylib libXpm.dylib libXpm 4:11:0 "$OBJ" "$OUT/system/libX11.dylib"
