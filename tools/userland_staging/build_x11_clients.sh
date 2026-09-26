#!/bin/bash
# Builds the X test clients xwd (xwd-1.0.9), xev (xev-1.2.6) and xdpyinfo
# (xdpyinfo-1.4.0), staged as libc_build/x11/server/root/usr/X11/bin/, from
# real, unmodified freedesktop sources (third_party/xwd, xev, xdpyinfo).
# DAR-383/DAR-387: xwd -root dumps the Xvfb framebuffer so it can be
# inspected on the host (tools/xwd_to_png.py); xev prints the events a
# window receives; xdpyinfo lists the server's screens and extensions.
# Toolchain, layout and helper functions: x11_common.sh / fontstack_common.sh.
#
# Sources: each Makefile.am's <prog>_SOURCES (x11_am_sources).
# config: x11_config/{xwd,xev,xdpyinfo}/config.h. Links: xwd's
# PKG_CHECK_MODULES (configure.ac:51, x11 plus xkbfile for XKB); xev's
# (configure.ac:41, xrandr and x11); xdpyinfo's (configure.ac:41, xext x11
# xtst x11-xcb xcb, plus xi and xrender from its optional checks).
set -e
cd "$(dirname "$0")"
. ./fontstack_common.sh

for d in libX11 libxkbfile libXrandr libXrender libXext libXtst libXi libX11-xcb libxcb; do
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- run build_$d.sh first" >&2; exit 1; }
done
x11_require_staged X11/extensions/XKBfile.h build_libxkbfile.sh
x11_require_sys_dylibs
STAGE="$X11_ROOT/server/root/usr/X11/bin"
mkdir -p "$STAGE"

build_client() {
    local name="$1" ver="$2"
    shift 2
    local src="$TP/$name"
    x11_require_source "$src/Makefile.am"
    grep -q "\[$name\], \[$ver\]" "$src/configure.ac" || { echo "error: $name is not $ver -- recheck this script" >&2; exit 1; }
    local obj="$X11_ROOT/obj/$name"
    rm -rf "$obj"
    mkdir -p "$obj"
    X11_OK=0 X11_FAIL=0
    for f in $(x11_am_sources "$src/Makefile.am" "${name}_SOURCES"); do
        x11_compile "$obj" "$src/$f" -I "$X11_CONFIG_DIR/$name" -I "$src"
    done
    x11_compile_report "$name"
    fs_link_exe "$STAGE/$name" "$obj" "$@"
}
build_client xwd 1.0.9 "$OUT/system/libxkbfile.dylib" "$OUT/system/libX11.dylib"
build_client xev 1.2.6 "$OUT/system/libXrandr.dylib" "$OUT/system/libXrender.dylib" \
    "$OUT/system/libXext.dylib" "$OUT/system/libX11.dylib"
build_client xdpyinfo 1.4.0 "$OUT/system/libXtst.dylib" "$OUT/system/libXi.dylib" \
    "$OUT/system/libXrender.dylib" "$OUT/system/libXext.dylib" "$OUT/system/libX11-xcb.dylib" \
    "$OUT/system/libxcb.dylib" "$OUT/system/libX11.dylib"
