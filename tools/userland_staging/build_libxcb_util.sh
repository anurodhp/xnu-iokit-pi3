#!/bin/bash
# Builds the xcb-util family as real dylibs from real, unmodified freedesktop
# sources (Gershwin WindowManager M1, docs/gershwin-feasibility.md). Toolchain,
# layout and helpers: x11_common.sh's header. One script, five libraries, in
# dependency order (the same shape as build_libxcb.sh's extension loop):
#
#   libxcb-util.dylib        xcb-util-0.4.1          /usr/X11/lib/libxcb-util.1.dylib
#       src/Makefile.am:11-14 -- atoms.c event.c xcb_aux.c; xcb_aux.h's xcb_aux_*
#       live here, there is no separate libxcb-aux. version-info 1:0:0.
#   libxcb-icccm.dylib       xcb-util-wm-0.4.2       /usr/X11/lib/libxcb-icccm.4.dylib
#       icccm/Makefile.am:11 version-info 4:0:0. libxcb-ewmh is not built: the
#       WindowManager has its own EWMHService.
#   libxcb-render-util.dylib xcb-util-renderutil-0.3.10  .../libxcb-render-util.0.dylib
#       renderutil/Makefile.am: cache.c util.c glyph.c, no version-info -> 0.
#   libxcb-image.dylib       xcb-util-image-0.4.1    /usr/X11/lib/libxcb-image.0.dylib
#       image/Makefile.am: xcb_image.c, links xcb-shm and xcb-util.
#   libxcb-cursor.dylib      xcb-util-cursor-0.1.6   /usr/X11/lib/libxcb-cursor.0.dylib
#       cursor/Makefile.am:13-16 -- cursor.c shape_to_id.c load_cursor.c
#       parse_cursor_file.c, links xcb-render, xcb-render-util and xcb-image.
#       shape_to_id.c is generated from shape_to_id.gperf with the same
#       GPERFFLAGS as cursor/Makefile.am:20 (host gperf, as c_client.py is
#       a host tool in build_libxcb.sh). The Makefile's -export-symbols-regex
#       '^xcb_cursor_' is -exported_symbol '_xcb_cursor_*'. XCURSORPATH is the
#       configure.ac DEF_CURSORPATH with ${datadir} = /usr/X11/share.
# Needs build_libxcb.sh first (libxcb, -shm, -render). config:
# x11_config/libxcb_util/config.h.
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

x11_require_source "$TP/xcb-util/src/atoms.c"
x11_require_source "$TP/xcb-util-wm/icccm/icccm.c"
x11_require_source "$TP/xcb-util-renderutil/renderutil/util.c"
x11_require_source "$TP/xcb-util-image/image/xcb_image.c"
x11_require_source "$TP/xcb-util-cursor/cursor/cursor.c"
for l in libxcb libxcb-shm libxcb-render; do
    [ -f "$OUT/system/$l.dylib" ] || { echo "error: $OUT/system/$l.dylib not found -- run build_libxcb.sh first" >&2; exit 1; }
done
x11_require_staged xcb/xcb.h build_libxcb.sh
x11_require_sys_dylibs
command -v gperf >/dev/null || { echo "error: gperf not found (needed for xcb-util-cursor's shape_to_id.c)" >&2; exit 1; }

FLAGS=(-DHAVE_CONFIG_H -I "$X11_CONFIG_DIR/libxcb_util" -I "$X11_INC")
SYS="$OUT/system"

# build <lib-stem> <version M:m:r> <srcdir> <objname> "<c files>" [extra link args...]
build() {
    local stem="$1" vn="$2" dir="$3" files="$4"
    shift 4
    local obj="$X11_ROOT/obj/$stem" f
    rm -rf "$obj"
    mkdir -p "$obj"
    X11_OK=0 X11_FAIL=0
    for f in $files; do
        case "$f" in /*) x11_compile "$obj" "$f" "${FLAGS[@]}" -I "$dir" "${EXTRA_CFLAGS[@]}" ;;
                     *)  x11_compile "$obj" "$dir/$f" "${FLAGS[@]}" -I "$dir" "${EXTRA_CFLAGS[@]}" ;;
        esac
    done
    x11_compile_report "$stem"
    x11_link_dylib "$stem.dylib" "$stem" "$vn" "$obj" "$@"
}
EXTRA_CFLAGS=()

x11_stage_headers xcb "$TP/xcb-util/src/xcb_util.h" "$TP/xcb-util/src/xcb_atom.h" \
    "$TP/xcb-util/src/xcb_aux.h" "$TP/xcb-util/src/xcb_event.h" \
    "$TP/xcb-util-wm/icccm/xcb_icccm.h" "$TP/xcb-util-renderutil/renderutil/xcb_renderutil.h" \
    "$TP/xcb-util-image/image/xcb_image.h" "$TP/xcb-util-image/image/xcb_pixel.h" \
    "$TP/xcb-util-image/image/xcb_bitops.h" "$TP/xcb-util-cursor/cursor/xcb_cursor.h"

build libxcb-util 1:0:0 "$TP/xcb-util/src" "atoms.c event.c xcb_aux.c" "$SYS/libxcb.dylib"
build libxcb-icccm 4:0:0 "$TP/xcb-util-wm/icccm" "icccm.c" "$SYS/libxcb.dylib"
build libxcb-render-util 0:0:0 "$TP/xcb-util-renderutil/renderutil" "cache.c util.c glyph.c" \
    "$SYS/libxcb.dylib" "$SYS/libxcb-render.dylib"
build libxcb-image 0:0:0 "$TP/xcb-util-image/image" "xcb_image.c" \
    "$SYS/libxcb.dylib" "$SYS/libxcb-shm.dylib" "$SYS/libxcb-util.dylib"

GEN="$X11_ROOT/gen/libxcb-cursor"
rm -rf "$GEN"
mkdir -p "$GEN"
gperf --includes --struct-type --language=ANSI-C --switch=1 \
    "$TP/xcb-util-cursor/cursor/shape_to_id.gperf" > "$GEN/shape_to_id.c" \
    || { echo "error: gperf failed on shape_to_id.gperf" >&2; exit 1; }
# The cursor sources never include config.h (autoconf passes the HAVE_*
# macros as -D, @DEFS@), so the one it tests goes on the command line.
EXTRA_CFLAGS=(-DHAVE_LIBKERN_OSBYTEORDER_H=1 "-DXCURSORPATH=\"~/.local/share/icons:~/.icons:/usr/X11/share/icons:/usr/X11/share/pixmaps\"")
build libxcb-cursor 0:0:0 "$TP/xcb-util-cursor/cursor" \
    "cursor.c $PWD/$GEN/shape_to_id.c load_cursor.c parse_cursor_file.c" \
    "$SYS/libxcb.dylib" "$SYS/libxcb-render.dylib" "$SYS/libxcb-render-util.dylib" "$SYS/libxcb-image.dylib" \
    -Wl,-exported_symbol,'_xcb_cursor_*'
