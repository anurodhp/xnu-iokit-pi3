#!/bin/bash
# Builds libc_build/system/libSM.dylib (install name
# /usr/X11/lib/libSM.6.dylib, -version-number 6:0:1) from real, unmodified
# libSM-1.2.6 (third_party/libSM, freedesktop xorg/lib/libsm). DAR-383, X11GS 2.
# Toolchain, layout and helper functions: x11_common.sh's header.
#
# Sources: src/Makefile.am's libSM_la_SOURCES, read with x11_am_sources.
# config: x11_config/libSM/config.h.
# No libuuid (see config.h), so client ids come from the xtrans network
# address path: src/sm_genid.c #includes <X11/Xtrans/Xtrans.h> for its
# TCPCONN/IPv6 address probing. Links libICE, libinfo (getaddrinfo) and
# libresolv (gethostname).
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

SRC="$TP/libSM"
x11_require_source "$SRC/src/Makefile.am"
if [ ! -f "$X11_INC/X11/Xmd.h" ]; then
    echo "error: $X11_INC/X11/Xmd.h not found -- run build_xorg_headers.sh first" >&2
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

OBJ="$X11_ROOT/obj/libSM"
rm -rf "$OBJ"
mkdir -p "$OBJ"
x11_stage_headers X11/SM "$SRC"/include/X11/SM/*.h
for f in $(x11_am_sources "$SRC/src/Makefile.am" libSM_la_SOURCES); do
    x11_compile "$OBJ" "$SRC/src/$f" -I "$X11_CONFIG_DIR/libSM" -I "$SRC/include"
done
x11_compile_report libSM
x11_link_dylib libSM.dylib libSM 6:0:1 "$OBJ" "$OUT/system/libICE.dylib" \
    "$OUT/system/libinfo.dylib" "$OUT/system/libresolv.dylib"
