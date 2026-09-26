#!/bin/bash
# Builds xkbcomp (staged as libc_build/x11/server/root/usr/X11/bin/xkbcomp)
# from real, unmodified xkbcomp-1.5.0 (third_party/xkbcomp). DAR-387,
# X11GS 4: the X server runs it to compile every keymap it loads.
# Toolchain, layout and helper functions: x11_common.sh / fontstack_common.sh.
#
# Sources: Makefile.am's xkbcomp_SOURCES (x11_am_sources), plus xkbparse.c
# generated from xkbparse.y on the host by /usr/bin/yacc -- configure.ac:45
# AC_PROG_YACC, the same step automake's .y rule runs (Apple's yacc is
# bison 2.3 in yacc mode).
# DFLT_XKB_CONFIG_ROOT: Makefile.am AM_CPPFLAGS, from configure.ac:63-68's
# default ${datadir}/X11/xkb = /usr/X11/share/X11/xkb under this port's
# /usr/X11 prefix. build_xvfb.sh points the server's XKB base at the same
# directory, and build_xkeyboard_config.sh installs the data there.
# config: x11_config/xkbcomp/config.h.
set -e
cd "$(dirname "$0")"
. ./fontstack_common.sh

SRC="$TP/xkbcomp"
x11_require_source "$SRC/Makefile.am"
grep -q '\[xkbcomp\], \[1.5.0\]' "$SRC/configure.ac" || { echo "error: xkbcomp is not 1.5.0 -- recheck this script" >&2; exit 1; }
x11_require_staged X11/extensions/XKBfile.h build_libxkbfile.sh
for d in libxkbfile libX11; do
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- run build_$d.sh first" >&2; exit 1; }
done
[ -x /usr/bin/yacc ] || { echo "error: /usr/bin/yacc not found (needed for xkbparse.y)" >&2; exit 1; }
x11_require_sys_dylibs

OBJ="$X11_ROOT/obj/xkbcomp"
GEN="$X11_ROOT/gen/xkbcomp"
STAGE="$X11_ROOT/server/root/usr/X11/bin"
rm -rf "$OBJ" "$GEN"
mkdir -p "$OBJ" "$GEN" "$STAGE"
/usr/bin/yacc -d -o "$GEN/xkbparse.c" "$SRC/xkbparse.y"
for f in $(x11_am_sources "$SRC/Makefile.am" xkbcomp_SOURCES); do
    x11_compile "$OBJ" "$SRC/$f" -I "$X11_CONFIG_DIR/xkbcomp" -I "$SRC" \
        -DDFLT_XKB_CONFIG_ROOT=\"/usr/X11/share/X11/xkb\"
done
x11_compile "$OBJ" "$GEN/xkbparse.c" -I "$X11_CONFIG_DIR/xkbcomp" -I "$SRC" -I "$GEN"
x11_compile_report xkbcomp
fs_link_exe "$STAGE/xkbcomp" "$OBJ" "$OUT/system/libxkbfile.dylib" "$OUT/system/libX11.dylib"
