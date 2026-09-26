#!/bin/bash
# Builds libc_build/system/libpixman-1.dylib (install name
# /usr/X11/lib/libpixman-1.0.dylib) from real, unmodified pixman 0.46.4
# (third_party/pixman, gitlab.freedesktop.org/pixman/pixman tag
# pixman-0.46.4 -- the version nixpkgs nixos-26.05 rev 8f0500b9 ships).
# DAR-384 (X11GS 3) and DAR-362 (Wayland 5) both need it: cairo's image
# backend and the X server / wlroots software renderer all draw through it.
#
# Follows pixman's own meson build (PureDarwin's recipe,
# nix/pkgs/x11/xvfb-pixman.nix, runs the same meson.build):
#   sources  -- pixman/meson.build pixman_files (read from the file), plus
#               the one SIMD library meson enables on AArch64: 'arm-neon'
#               with have_a64neon (pixman/meson.build simds table) =
#               pixman-arm-neon.c + pixman-arma64-neon-asm.S +
#               pixman-arma64-neon-asm-bilinear.S. The A64 NEON paths are
#               the Cortex-A53 speedup the ticket asks to consider;
#               pixman-arm.c enables them unconditionally on AArch64.
#   config   -- fontstack_config/pixman/pixman-config.h (every define
#               cites its meson.build line); pixman-version.h generated
#               from pixman-version.h.in the way meson.build:592-596 does
#   flags    -- -DHAVE_CONFIG_H (meson.build:598)
#   name     -- library('pixman-1', version : '0.46.4'): meson's darwin
#               naming is libpixman-1.0.dylib with compatibility and current
#               version 0.0.0 (what meson and Homebrew's pixman produce)
# Headers pixman.h + pixman-version.h go to $X11_INC/pixman-1.
set -e
cd "$(dirname "$0")"
. ./fontstack_common.sh

SRC="$TP/pixman"
x11_require_source "$SRC/pixman/meson.build"
[ -f "$OUT/system/libsystem_m.dylib" ] || { echo "error: $OUT/system/libsystem_m.dylib not found -- run build_libm_dylib.sh first" >&2; exit 1; }
for d in libsystem_kernel libsystem_c libsystem_pthread libsystem_malloc libsystem_platform libdyld; do
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2; exit 1; }
done
x11_require_sys_dylibs
grep -q "version : '0.46.4'" "$SRC/meson.build" || { echo "error: pixman is not 0.46.4 -- recheck this script" >&2; exit 1; }

OBJ="$FS_ROOT/obj/pixman"
GEN="$FS_ROOT/gen/pixman"
rm -rf "$OBJ" "$GEN"
mkdir -p "$OBJ" "$GEN"
sed -e 's/@PIXMAN_VERSION_MAJOR@/0/g' -e 's/@PIXMAN_VERSION_MINOR@/46/g' -e 's/@PIXMAN_VERSION_MICRO@/4/g' \
    "$SRC/pixman/pixman-version.h.in" > "$GEN/pixman-version.h"
cp "$FS_CONFIG_DIR/pixman/pixman-config.h" "$GEN/"

SRCS=$(awk '/^pixman_files = files\(/{f=1; next} f && /^\)/{f=0} f {gsub(/[ ,\x27]/, ""); if ($0 != "") print}' "$SRC/pixman/meson.build")
[ "$(echo "$SRCS" | wc -l)" -ge 30 ] || { echo "error: could not read pixman_files from pixman/meson.build" >&2; exit 1; }
for f in $SRCS pixman-arm-neon.c pixman-arma64-neon-asm.S pixman-arma64-neon-asm-bilinear.S; do
    x11_compile "$OBJ" "$SRC/pixman/$f" -I "$GEN" -I "$SRC/pixman"
done
x11_compile_report pixman

mkdir -p "$X11_INC/pixman-1"
cp "$SRC/pixman/pixman.h" "$GEN/pixman-version.h" "$X11_INC/pixman-1/"

fs_link_dylib libpixman-1.dylib "$X11_PREFIX/lib/libpixman-1.0.dylib" 0 0 "$OBJ" \
    "$OUT/system/libsystem_m.dylib"
