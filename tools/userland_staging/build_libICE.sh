#!/bin/bash
# Builds libc_build/system/libICE.dylib (install name
# /usr/X11/lib/libICE.6.dylib, -version-number 6:3:0) from real, unmodified
# libICE-1.1.2 (third_party/libICE, freedesktop xorg/lib/libice). DAR-383, X11GS 2.
# Toolchain, layout and helper functions: x11_common.sh's header.
#
# Sources: src/Makefile.am's libICE_la_SOURCES, read with x11_am_sources.
# config: x11_config/libICE/config.h.
# Xtrans is compiled in the way libICE does it: src/icetrans.c #includes
# <X11/Xtrans/transport.c> with ICE_t/TRANS_CLIENT/TRANS_SERVER defined
# (config.h). Links libinfo (getaddrinfo) and libresolv (inet_ntop,
# gethostname).
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

SRC="$TP/libICE"
x11_require_source "$SRC/src/Makefile.am"
if [ ! -f "$X11_INC/X11/Xmd.h" ]; then
    echo "error: $X11_INC/X11/Xmd.h not found -- run build_xorg_headers.sh first" >&2
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

OBJ="$X11_ROOT/obj/libICE"
rm -rf "$OBJ"
mkdir -p "$OBJ"
x11_stage_headers X11/ICE "$SRC"/include/X11/ICE/*.h
for f in $(x11_am_sources "$SRC/src/Makefile.am" libICE_la_SOURCES); do
    x11_compile "$OBJ" "$SRC/src/$f" -I "$X11_CONFIG_DIR/libICE" -I "$SRC/include"
done
x11_compile_report libICE
x11_link_dylib libICE.dylib libICE 6:3:0 "$OBJ" "$OUT/system/libinfo.dylib" "$OUT/system/libresolv.dylib"
