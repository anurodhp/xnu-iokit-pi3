#!/bin/bash
# Builds libc_build/system/libXfont2.dylib (install name
# /usr/X11/lib/libXfont2.2.dylib, -version-number 2:0:0) from real,
# unmodified libXfont2-2.0.7 (third_party/libXfont2). DAR-387, X11GS 4: the X
# server's font backend.
# Toolchain, layout and helper functions: x11_common.sh / fontstack_common.sh.
#
# Sources: Makefile.am's libXfont2_la_SOURCES, read with x11_am_sources.
# That reads every `+=`, including the conditional blocks, so the ones this
# configuration leaves off are dropped by directory (Makefile.am:120-160):
#   src/fc/          XFONT_FC off (x11_config/libXfont2/config.h, CA:215-222)
#   snfread.c        XFONT_SNFFORMAT off (CA:190-197)
#   bunzip2.c        X_BZIP2_FONT_COMPRESSION off (CA:147-153)
# plus the AC_REPLACE_FUNCS replacement src/util/reallocarray.c (see
# config.h).
# Flags: -I$SRC is automake's DEFAULT_INCLUDES (sources include
# "src/util/replace.h"); -fvisibility=hidden, as configure.ac:240-253 adds it whenever the
# compiler supports it, so only _X_EXPORT symbols are exported.
set -e
cd "$(dirname "$0")"
. ./fontstack_common.sh

SRC="$TP/libXfont2"
x11_require_source "$SRC/Makefile.am"
grep -q '\[libXfont2\], \[2.0.7\]' "$SRC/configure.ac" || { echo "error: libXfont2 is not 2.0.7 -- recheck this script" >&2; exit 1; }
x11_require_staged X11/fonts/fontproto.h build_xorg_headers.sh
x11_require_staged X11/fonts/fontenc.h build_libfontenc.sh
for d in libfontenc libfreetype libz libsystem_m; do
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- build it first" >&2; exit 1; }
done
[ -d "$X11_INC/freetype2" ] || { echo "error: $X11_INC/freetype2 not staged -- run build_freetype.sh first" >&2; exit 1; }
x11_require_sys_dylibs

OBJ="$X11_ROOT/obj/libXfont2"
rm -rf "$OBJ"
mkdir -p "$OBJ"
SRCS=$(x11_am_sources "$SRC/Makefile.am" libXfont2_la_SOURCES | grep -v -e '^src/fc/' -e 'snfread\.c$' -e 'bunzip2\.c$')
[ "$(echo "$SRCS" | wc -l)" -ge 40 ] || { echo "error: could not read libXfont2_la_SOURCES from Makefile.am" >&2; exit 1; }
for f in $SRCS src/util/reallocarray.c; do
    x11_compile "$OBJ" "$SRC/$f" -fvisibility=hidden -I "$X11_CONFIG_DIR/libXfont2" \
        -I "$SRC" -I "$SRC/include" -I "$X11_INC/freetype2"
done
x11_compile_report libXfont2
x11_stage_headers X11/fonts "$SRC/include/X11/fonts/libxfont2.h"
x11_link_dylib libXfont2.dylib libXfont2 2:0:0 "$OBJ" \
    "$OUT/system/libfontenc.dylib" "$OUT/system/libfreetype.dylib" \
    "$OUT/system/libz.dylib" "$OUT/system/libsystem_m.dylib"
