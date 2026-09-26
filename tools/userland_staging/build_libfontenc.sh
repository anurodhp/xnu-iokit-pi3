#!/bin/bash
# Builds libc_build/system/libfontenc.dylib (install name
# /usr/X11/lib/libfontenc.1.dylib, -version-number 1:0:0) for the TARGET
# from real, unmodified libfontenc-1.1.9 (third_party/libfontenc, the same
# pin build_fonts.sh already builds for the host). DAR-387, X11GS 4:
# libXfont2, and so Xvfb, link it.
# Toolchain, layout and helper functions: x11_common.sh / fontstack_common.sh.
#
# Sources: src/Makefile.am's libfontenc_la_SOURCES (read with
# x11_am_sources) plus src/reallocarray.c, the AC_REPLACE_FUNCS
# replacement (config: x11_config/libfontenc/config.h).
# FONT_ENCODINGS_DIRECTORY: src/Makefile.am's FONTENCDEFS,
# "$(ENCODINGSDIR)/encodings.dir". ENCODINGSDIR defaults to
# FONTROOTDIR/encodings with FONTROOTDIR = ${datadir}/fonts/X11
# (font-util fontutil.m4.in:358-364, 377-384); this uses
# /usr/X11/share/fonts/encodings instead, matching the host build in
# build_fonts.sh and Apple's own /usr/X11/share/fonts layout.
set -e
cd "$(dirname "$0")"
. ./fontstack_common.sh

SRC="$TP/libfontenc"
x11_require_source "$SRC/src/Makefile.am"
grep -q '\[libfontenc\], \[1.1.9\]' "$SRC/configure.ac" || { echo "error: libfontenc is not 1.1.9 -- recheck this script" >&2; exit 1; }
x11_require_staged X11/Xproto.h build_xorg_headers.sh
[ -f "$OUT/system/libz.dylib" ] || { echo "error: $OUT/system/libz.dylib not found -- run build_zlib.sh first" >&2; exit 1; }
[ -f "$FS_SYSINC/zlib.h" ] || { echo "error: $FS_SYSINC/zlib.h not staged -- run build_zlib.sh first" >&2; exit 1; }
x11_require_sys_dylibs

OBJ="$X11_ROOT/obj/libfontenc"
rm -rf "$OBJ"
mkdir -p "$OBJ"
for f in $(x11_am_sources "$SRC/src/Makefile.am" libfontenc_la_SOURCES) reallocarray.c; do
    x11_compile "$OBJ" "$SRC/src/$f" -I "$X11_CONFIG_DIR/libfontenc" -I "$SRC/include" \
        -DFONT_ENCODINGS_DIRECTORY=\"/usr/X11/share/fonts/encodings/encodings.dir\"
done
x11_compile_report libfontenc
x11_stage_headers X11/fonts "$SRC/include/X11/fonts/fontenc.h"
x11_link_dylib libfontenc.dylib libfontenc 1:0:0 "$OBJ" "$OUT/system/libz.dylib"
