#!/bin/bash
# libepoxy 1.5.10 (GL function-pointer dispatch; the WebKit port requires it
# even when GL is unused, docs/webkit-browser-feasibility.md) cross-built for
# the target: /usr/X11/lib/libepoxy.0.dylib, GLX only (no EGL: Mesa's EGL
# cannot be built here, docs/gpu-opengl-plan.md).
#
# Unpatched upstream. On Darwin epoxy loads GLX from the XQuartz path
# "/opt/X11/lib/libGL.1.dylib" (src/dispatch_common.c:177) and GL from
# "/System/Library/Frameworks/OpenGL.framework/Versions/Current/OpenGL" (:669,
# and it abort()s if that cannot be opened -- found by running
# epoxy_smoketest). This system's X11 lives in /usr/X11, so
# inject_into_sd_image.sh adds symlinks for both paths instead of patching.
#
# Reuses the Mesa cross file (build_mesa.sh must have run once).
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

SRC="$TP/libepoxy"
x11_require_source "$SRC/meson.build"
GEN="$PWD/$X11_ROOT/gen/mesa"
[ -f "$GEN/cross.ini" ] || { echo "error: $GEN/cross.ini not found -- run build_mesa.sh first" >&2; exit 1; }
x11_require_staged GL/gl.h build_mesa.sh
x11_require_sys_dylibs
for t in meson ninja pkg-config; do
    command -v "$t" >/dev/null || { echo "error: $t not found on the host" >&2; exit 1; }
done
grep -q "version: '1.5.10'" "$SRC/meson.build" || { echo "error: libepoxy is not 1.5.10 -- recheck this script" >&2; exit 1; }

BUILD="$PWD/$X11_ROOT/gen/libepoxy-build"
rm -rf "$BUILD"
# The Mesa cross file's -I/pkg-config settings are what this needs too.
meson setup "$BUILD" "$SRC" --cross-file "$GEN/cross.ini" \
    --prefix=/usr/X11 --buildtype=plain \
    -Dglx=yes -Degl=no -Dx11=true -Dtests=false -Ddocs=false \
    > "$GEN/epoxy-setup.log" 2>&1 || { tail -30 "$GEN/epoxy-setup.log" >&2; exit 1; }
ninja -C "$BUILD" > "$GEN/epoxy-ninja.log" 2>&1 || { grep -E 'error:|FAILED' "$GEN/epoxy-ninja.log" | head -20 >&2; exit 1; }

BUILT=$(ls "$BUILD"/src/libepoxy.0.dylib 2>/dev/null || ls "$BUILD"/src/libepoxy*.dylib | head -1)
OUTLIB="$OUT/system/libepoxy.dylib"
cp "$BUILT" "$OUTLIB"
install_name_tool -id "$X11_PREFIX/lib/libepoxy.0.dylib" "$OUTLIB"
otool -hv "$OUTLIB" | grep -q " DYLIB " || { echo "error: $OUTLIB is not MH_DYLIB" >&2; exit 1; }
mkdir -p "$X11_INC/epoxy"
cp "$SRC"/include/epoxy/*.h "$BUILD"/include/epoxy/*.h "$X11_INC/epoxy/" 2>/dev/null || cp "$SRC"/include/epoxy/*.h "$X11_INC/epoxy/"
cp "$BUILD"/src/*.h "$X11_INC/epoxy/" 2>/dev/null || true
echo "wrote $OUTLIB ($X11_PREFIX/lib/libepoxy.0.dylib)"
./bind_target_audit.sh "$OUTLIB"
