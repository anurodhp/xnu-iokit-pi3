#!/bin/bash
# Builds libc_build/system/libXau.dylib (install name
# /usr/X11/lib/libXau.6.dylib) from real, unmodified libXau-1.0.12
# (third_party/libXau, freedesktop xorg/lib/libxau). DAR-383, X11GS 2.
# Toolchain, layout and helper functions: x11_common.sh's header.
#
# Sources: Makefile.am's libXau_la_SOURCES, all 8 (Autest.c is its
# check_PROGRAMS test driver, not part of the library).
# config: x11_config/libXau/config.h.
#
# Links libremovefile.dylib: in this tree it is the one dylib exporting
# rename() (AuLock.c/AuUnlock.c/AuWrite.c paths use link/rename/unlink for
# the .Xauthority lock dance); real Apple's owner is libsystem_kernel
# (libsyscall/wrappers/rename.c), which here has only the raw trap.
# libremovefile is re-exported by libSystem.B, as on real Apple.
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

SRC="$TP/libXau"
x11_require_source "$SRC/AuRead.c"
if [ ! -f "$X11_INC/X11/Xmd.h" ]; then
    echo "error: $X11_INC/X11/Xmd.h not found -- run build_xorg_headers.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libremovefile.dylib" ]; then
    echo "error: $OUT/system/libremovefile.dylib not found -- run build_removefile_dylib.sh first" >&2
    exit 1
fi
for d in libsystem_kernel libsystem_c libsystem_pthread libsystem_malloc libsystem_platform libdyld; do
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2; exit 1; }
done
x11_require_sys_dylibs

OBJ="$X11_ROOT/obj/libXau"
rm -rf "$OBJ"
mkdir -p "$OBJ"
x11_stage_headers X11 "$SRC/include/X11/Xauth.h"

for f in AuDispose AuFileName AuGetAddr AuGetBest AuLock AuRead AuUnlock AuWrite; do
    x11_compile "$OBJ" "$SRC/$f.c" -I "$X11_CONFIG_DIR/libXau" -I "$SRC/include"
done
x11_compile_report libXau

x11_link_dylib libXau.dylib libXau 6:0:0 "$OBJ" "$OUT/system/libremovefile.dylib"
