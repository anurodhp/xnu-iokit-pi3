#!/bin/bash
# Builds libc_build/system/libXdmcp.dylib (install name
# /usr/X11/lib/libXdmcp.6.dylib) from real, unmodified libXdmcp-1.1.5
# (third_party/libXdmcp, freedesktop xorg/lib/libxdmcp). DAR-383, X11GS 2.
# Toolchain, layout and helper functions: x11_common.sh's header.
#
# Sources: Makefile.am's libXdmcp_la_SOURCES plus Wraphelp.c
# (nodist_libXdmcp_la_SOURCES under HASXDMAUTH, which configure.ac sets
# because the file is present). config: x11_config/libXdmcp/config.h.
#
# Links libresolv.dylib: this tree's public owner of sendto() (Flush.c),
# see build_libsystem_kernel.sh's -unexported_symbol,_sendto comment.
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

SRC="$TP/libXdmcp"
x11_require_source "$SRC/Wraphelp.c"
if [ ! -f "$X11_INC/X11/Xmd.h" ]; then
    echo "error: $X11_INC/X11/Xmd.h not found -- run build_xorg_headers.sh first" >&2
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

OBJ="$X11_ROOT/obj/libXdmcp"
rm -rf "$OBJ"
mkdir -p "$OBJ"
x11_stage_headers X11 "$SRC/include/X11/Xdmcp.h"

for f in Array Fill Flush Key Read Unwrap Wrap Write Wraphelp; do
    x11_compile "$OBJ" "$SRC/$f.c" -I "$X11_CONFIG_DIR/libXdmcp" -I "$SRC/include"
done
x11_compile_report libXdmcp

x11_link_dylib libXdmcp.dylib libXdmcp 6:0:0 "$OBJ" "$OUT/system/libresolv.dylib"
