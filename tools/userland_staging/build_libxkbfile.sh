#!/bin/bash
# Builds libc_build/system/libxkbfile.dylib (install name
# /usr/X11/lib/libxkbfile.1.dylib, -version-number 1:0:2) from real,
# unmodified libxkbfile-1.1.3 (third_party/libxkbfile). DAR-387, X11GS 4:
# xkbcomp links it to compile the keymaps the X server asks for.
# Toolchain, layout and helper functions: x11_common.sh.
#
# Sources: src/Makefile.am's libxkbfile_la_SOURCES (x11_am_sources), with
# its AM_CPPFLAGS (-I include, -I include/X11/extensions).
# config: x11_config/libxkbfile/config.h.
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

SRC="$TP/libxkbfile"
x11_require_source "$SRC/src/Makefile.am"
grep -q '\[libxkbfile\], \[1.1.3\]' "$SRC/configure.ac" || { echo "error: libxkbfile is not 1.1.3 -- recheck this script" >&2; exit 1; }
x11_require_staged X11/extensions/XKBproto.h build_xorg_headers.sh
[ -f "$OUT/system/libX11.dylib" ] || { echo "error: $OUT/system/libX11.dylib not found -- run build_libX11.sh first" >&2; exit 1; }
x11_require_sys_dylibs

OBJ="$X11_ROOT/obj/libxkbfile"
rm -rf "$OBJ"
mkdir -p "$OBJ"
for f in $(x11_am_sources "$SRC/src/Makefile.am" libxkbfile_la_SOURCES); do
    x11_compile "$OBJ" "$SRC/src/$f" -I "$X11_CONFIG_DIR/libxkbfile" \
        -I "$SRC/include" -I "$SRC/include/X11/extensions"
done
x11_compile_report libxkbfile
x11_stage_headers X11/extensions "$SRC"/include/X11/extensions/XKM.h "$SRC"/include/X11/extensions/XKBrules.h \
    "$SRC"/include/X11/extensions/XKBbells.h "$SRC"/include/X11/extensions/XKBconfig.h \
    "$SRC"/include/X11/extensions/XKMformat.h "$SRC"/include/X11/extensions/XKBfile.h
x11_link_dylib libxkbfile.dylib libxkbfile 1:0:2 "$OBJ" "$OUT/system/libX11.dylib"
