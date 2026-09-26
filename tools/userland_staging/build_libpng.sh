#!/bin/bash
# Builds libc_build/system/libpng16.dylib (install name
# /usr/X11/lib/libpng16.16.dylib) from real, unmodified libpng 1.6.58
# (third_party/libpng, github.com/pnggroup/libpng tag v1.6.58 -- the
# version nixpkgs nixos-26.05 rev 8f0500b9 ships; nixpkgs' optional APNG
# patch is not applied, cairo does not use it). DAR-384, X11GS 3: cairo's
# PNG reader/writer (cairo_surface_write_to_png).
#
# Recipe:
#   sources   -- Makefile.am:127 libpng16_la_SOURCES (png.c ... pngwutil.c)
#                plus the arm/ sources (Makefile.am:133-135), which
#                CMakeLists.txt:163-186 turns on for arm64 by default with
#                -DPNG_ARM_NEON_OPT=2 (NEON always on; every ARMv8-A core,
#                the Pi 3's Cortex-A53 included, has Advanced SIMD)
#   pnglibconf.h -- scripts/pnglibconf.h.prebuilt, libpng's own default
#                configuration (CMakeLists.txt:455-460 uses exactly this
#                file when it does not regenerate it from pnglibconf.dfa;
#                regenerating with no pngusr.dfa options gives the same
#                settings). Includes PNG_ZLIB_VERNUM 0 = "do not check".
#   version   -- libtool -version-number 16:58:0 (Makefile.am's
#                @PNGLIB_MAJOR@@PNGLIB_MINOR@:@PNGLIB_RELEASE@:0, with
#                configure.ac:52 PNGLIB_RELEASE=58): libpng16.16.dylib,
#                compat 75, current 75.0 -- the same numbers an autotools
#                libpng 1.6.58 build gets on macOS
# Headers: png.h/pngconf.h/pnglibconf.h under $X11_INC/libpng16 and,
# as `make install` links them, $X11_INC itself.
set -e
cd "$(dirname "$0")"
. ./fontstack_common.sh

SRC="$TP/libpng"
x11_require_source "$SRC/Makefile.am"
[ -f "$OUT/system/libz.dylib" ] || { echo "error: $OUT/system/libz.dylib not found -- run build_zlib.sh first" >&2; exit 1; }
for d in libsystem_kernel libsystem_c libsystem_pthread libsystem_malloc libsystem_platform libdyld; do
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2; exit 1; }
done
x11_require_sys_dylibs
grep -q '^PNGLIB_RELEASE=58$' "$SRC/configure.ac" || { echo "error: libpng is not 1.6.58 -- recheck this script" >&2; exit 1; }

OBJ="$FS_ROOT/obj/libpng"
GEN="$FS_ROOT/gen/libpng"
rm -rf "$OBJ" "$GEN"
mkdir -p "$OBJ" "$GEN"
cp "$SRC/scripts/pnglibconf.h.prebuilt" "$GEN/pnglibconf.h"

SRCS=$(x11_am_sources "$SRC/Makefile.am" 'libpng@PNGLIB_MAJOR@@PNGLIB_MINOR@_la_SOURCES' | grep -v /)
[ "$(echo "$SRCS" | wc -l)" -eq 15 ] || { echo "error: expected 15 core sources from Makefile.am, got: $SRCS" >&2; exit 1; }
for f in $SRCS arm/arm_init.c arm/filter_neon_intrinsics.c arm/palette_neon_intrinsics.c; do
    # -UHAVE_CONFIG_H: x11_common.sh's base flags define it for the X.org
    # autoconf builds; libpng's CMake build (the one followed here) has no
    # config.h, and pngpriv.h:71 would include one.
    x11_compile "$OBJ" "$SRC/$f" -UHAVE_CONFIG_H -DPNG_ARM_NEON_OPT=2 -I "$GEN" -I "$SRC"
done
x11_compile_report libpng

mkdir -p "$X11_INC/libpng16"
cp "$SRC/png.h" "$SRC/pngconf.h" "$GEN/pnglibconf.h" "$X11_INC/libpng16/"
cp "$SRC/png.h" "$SRC/pngconf.h" "$GEN/pnglibconf.h" "$X11_INC/"

read -r major compat current <<< "$(x11_dylib_versions 16:58:0)"
fs_link_dylib libpng16.dylib "$X11_PREFIX/lib/libpng16.$major.dylib" "$compat" "$current" "$OBJ" \
    "$OUT/system/libz.dylib"
