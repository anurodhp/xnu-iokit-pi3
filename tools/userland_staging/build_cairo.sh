#!/bin/bash
# Builds libc_build/system/libcairo.dylib (install name
# /usr/X11/lib/libcairo.2.dylib) from real, unmodified cairo 1.18.4
# (third_party/cairo, gitlab.freedesktop.org/cairo/cairo tag 1.18.4 -- the
# version nixpkgs nixos-26.05 rev 8f0500b9 ships). DAR-384, X11GS 3: the
# 2D library gnustep-back's cairo backend draws with.
# Helpers: fontstack_common.sh / x11_common.sh.
#
# Follows cairo's own meson build (PureDarwin's recipe,
# nix/pkgs/gtk/cairo.nix / cairoArm64Build, runs the same meson.build),
# configured with only what the ticket asks for: the image and xlib
# (+Xrender) surfaces, the FreeType and fontconfig font backends and the
# PNG functions. Off: xcb, zlib (script/PS/PDF), quartz, glib/gobject,
# GL, tee, lzo, spectre, symbol-lookup, tests. The SVG surface comes along
# with PNG -- meson.build couples them (see cairo-features.h).
#
#   config.h / cairo-features.h -- fontstack_config/cairo/, every define
#       citing its meson.build line
#   sources  -- src/meson.build cairo_sources plus cairo_feature_sources
#       for the built features whose names have source lists: cairo-png,
#       cairo-svg, cairo-ft, cairo-xlib (cairo-fc and cairo-xlib-xrender
#       have none). Read from src/meson.build below.
#   flags    -- -D_GNU_SOURCE (meson.build:100), -D_REENTRANT (the
#       check_thread_flags entry that linked, :169), -DCAIRO_COMPILATION
#       and gnu_symbol_visibility hidden (src/meson.build's library()),
#       -fno-strict-aliasing -fno-common (meson.build's cflags list)
#   name     -- soversion 2 (meson.build:27, major+1) -> libcairo.2.dylib;
#       compatibility and current version 2, what meson emits for
#       version '2.11804.4' on darwin
# Links pixman, freetype, fontconfig, libpng16, libX11, libXext,
# libXrender and libsystem_m (m_dep).
set -e
cd "$(dirname "$0")"
. ./fontstack_common.sh

SRC="$TP/cairo"
x11_require_source "$SRC/src/meson.build"
[ -f "$OUT/system/libpixman-1.dylib" ] || { echo "error: $OUT/system/libpixman-1.dylib not found -- run build_pixman.sh first" >&2; exit 1; }
[ -f "$OUT/system/libfreetype.dylib" ] || { echo "error: $OUT/system/libfreetype.dylib not found -- run build_freetype.sh first" >&2; exit 1; }
[ -f "$OUT/system/libfontconfig.dylib" ] || { echo "error: $OUT/system/libfontconfig.dylib not found -- run build_fontconfig.sh first" >&2; exit 1; }
[ -f "$OUT/system/libpng16.dylib" ] || { echo "error: $OUT/system/libpng16.dylib not found -- run build_libpng.sh first" >&2; exit 1; }
[ -f "$OUT/system/libX11.dylib" ] || { echo "error: $OUT/system/libX11.dylib not found -- run build_libX11.sh first" >&2; exit 1; }
[ -f "$OUT/system/libXext.dylib" ] || { echo "error: $OUT/system/libXext.dylib not found -- run build_libXext.sh first" >&2; exit 1; }
[ -f "$OUT/system/libXrender.dylib" ] || { echo "error: $OUT/system/libXrender.dylib not found -- run build_libXrender.sh first" >&2; exit 1; }
[ -f "$OUT/system/libsystem_m.dylib" ] || { echo "error: $OUT/system/libsystem_m.dylib not found -- run build_libm_dylib.sh first" >&2; exit 1; }
for d in libsystem_kernel libsystem_c libsystem_pthread libsystem_malloc libsystem_platform libdyld; do
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2; exit 1; }
done
x11_require_sys_dylibs
grep -q '^1\.18\.4' <(python3 "$SRC/version.py") || { echo "error: cairo is not 1.18.4 -- recheck this script" >&2; exit 1; }

OBJ="$FS_ROOT/obj/cairo"
GEN="$FS_ROOT/gen/cairo"
rm -rf "$OBJ" "$GEN"
mkdir -p "$OBJ" "$GEN"
cp "$FS_CONFIG_DIR/cairo/config.h" "$FS_CONFIG_DIR/cairo/cairo-features.h" "$GEN/"

SRCS=$(python3 - "$SRC/src/meson.build" <<'EOF'
import re, sys
text = open(sys.argv[1]).read()
base = re.search(r"^cairo_sources = \[(.*?)^\]", text, re.S | re.M).group(1)
feat = re.search(r"^cairo_feature_sources = \{(.*?)^\}", text, re.S | re.M).group(1)
out = re.findall(r"'([^']+\.c)'", base)
for key in ('cairo-png', 'cairo-svg', 'cairo-ft', 'cairo-xlib'):
    m = re.search(r"'%s': \[(.*?)\]" % re.escape(key), feat, re.S)
    if not m:
        sys.exit('no cairo_feature_sources entry for ' + key)
    out += re.findall(r"'([^']+\.c)'", m.group(1))
print('\n'.join(out))
EOF
)
[ "$(echo "$SRCS" | wc -l)" -eq 132 ] || { echo "error: expected 132 cairo sources (meson's own count for this configuration), got $(echo "$SRCS" | wc -l)" >&2; exit 1; }
for f in $SRCS; do
    x11_compile "$OBJ" "$SRC/src/$f" -D_GNU_SOURCE -D_REENTRANT -DCAIRO_COMPILATION \
        -fvisibility=hidden -fno-strict-aliasing \
        -I "$GEN" -I "$SRC/src" -I "$SRC" \
        -I "$X11_INC/freetype2" -I "$X11_INC/pixman-1" -I "$X11_INC/libpng16"
done
x11_compile_report cairo

mkdir -p "$X11_INC/cairo"
for h in cairo.h cairo-version.h cairo-deprecated.h cairo-svg.h cairo-ft.h cairo-xlib.h cairo-xlib-xrender.h; do
    cp "$SRC/src/$h" "$X11_INC/cairo/"
done
cp "$GEN/cairo-features.h" "$X11_INC/cairo/"

fs_link_dylib libcairo.dylib "$X11_PREFIX/lib/libcairo.2.dylib" 2 2 "$OBJ" \
    "$OUT/system/libpixman-1.dylib" "$OUT/system/libfontconfig.dylib" "$OUT/system/libfreetype.dylib" \
    "$OUT/system/libpng16.dylib" "$OUT/system/libXrender.dylib" "$OUT/system/libXext.dylib" \
    "$OUT/system/libX11.dylib" "$OUT/system/libsystem_m.dylib"
