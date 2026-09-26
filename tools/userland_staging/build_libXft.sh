#!/bin/bash
# Builds libc_build/system/libXft.dylib (install name
# /usr/X11/lib/libXft.2.dylib, -version-number 2:3:9) from real, unmodified
# libXft-2.3.9 (third_party/libXft, freedesktop xorg/lib/libxft; pinned by
# DAR-383, built here by DAR-384 once freetype and fontconfig existed).
# gnustep-back's xlib graphics backend draws text through it.
# Toolchain, layout and helper functions: x11_common.sh's header.
#
# Sources: src/Makefile.am's libXft_la_SOURCES, read with x11_am_sources.
# config: x11_config/libXft/config.h. Xft.h: include/X11/Xft/Xft.h.in with
# configure.ac:50-59's XFT_MAJOR/XFT_MINOR/XFT_REVISION (2, 3, 9)
# substituted for its three `#undef` lines, as AC_CONFIG_HEADERS does.
# Links libfontconfig, libfreetype, libXrender and libX11
# (libXft_la_LIBADD).
set -e
cd "$(dirname "$0")"
. ./fontstack_common.sh

SRC="$TP/libXft"
x11_require_source "$SRC/src/Makefile.am"
if [ ! -f "$X11_INC/X11/Xmd.h" ]; then
    echo "error: $X11_INC/X11/Xmd.h not found -- run build_xorg_headers.sh first" >&2
    exit 1
fi
[ -f "$OUT/system/libX11.dylib" ] || { echo "error: $OUT/system/libX11.dylib not found -- run build_libX11.sh first" >&2; exit 1; }
[ -f "$OUT/system/libXrender.dylib" ] || { echo "error: $OUT/system/libXrender.dylib not found -- run build_libXrender.sh first" >&2; exit 1; }
[ -f "$OUT/system/libfreetype.dylib" ] || { echo "error: $OUT/system/libfreetype.dylib not found -- run build_freetype.sh first" >&2; exit 1; }
[ -f "$OUT/system/libfontconfig.dylib" ] || { echo "error: $OUT/system/libfontconfig.dylib not found -- run build_fontconfig.sh first" >&2; exit 1; }
for d in libsystem_kernel libsystem_c libsystem_pthread libsystem_malloc libsystem_platform libdyld; do
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2; exit 1; }
done
x11_require_sys_dylibs
grep -q 'AC_INIT(\[libXft\], \[2.3.9\]' "$SRC/configure.ac" || { echo "error: libXft is not 2.3.9 -- recheck this script" >&2; exit 1; }

OBJ="$X11_ROOT/obj/libXft"
GEN="$X11_ROOT/gen/libXft"
rm -rf "$OBJ" "$GEN"
mkdir -p "$OBJ" "$GEN/X11/Xft"
sed -e 's/^#undef XFT_MAJOR$/#define XFT_MAJOR 2/' \
    -e 's/^#undef XFT_MINOR$/#define XFT_MINOR 3/' \
    -e 's/^#undef XFT_REVISION$/#define XFT_REVISION 9/' \
    "$SRC/include/X11/Xft/Xft.h.in" > "$GEN/X11/Xft/Xft.h"
[ "$(grep -c '^#define XFT_\(MAJOR\|MINOR\|REVISION\) ' "$GEN/X11/Xft/Xft.h")" -eq 3 ] || {
    echo "error: Xft.h.in substitution did not apply" >&2; exit 1; }
x11_stage_headers X11/Xft "$GEN/X11/Xft/Xft.h" "$SRC/include/X11/Xft/XftCompat.h"

for f in $(x11_am_sources "$SRC/src/Makefile.am" libXft_la_SOURCES); do
    x11_compile "$OBJ" "$SRC/src/$f" -I "$X11_CONFIG_DIR/libXft" -I "$SRC/src" \
        -I "$GEN/X11/Xft" -I "$X11_INC/freetype2"
done
x11_compile_report libXft
x11_link_dylib libXft.dylib libXft 2:3:9 "$OBJ" \
    "$OUT/system/libfontconfig.dylib" "$OUT/system/libfreetype.dylib" \
    "$OUT/system/libXrender.dylib" "$OUT/system/libX11.dylib"
