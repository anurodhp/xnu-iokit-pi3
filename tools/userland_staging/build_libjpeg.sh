#!/bin/bash
# Builds libc_build/system/libjpeg.dylib (install name /usr/X11/lib/libjpeg.62.dylib) from real, unmodified
# libjpeg-turbo 3.0.4 (third_party/libjpeg-turbo, github.com/libjpeg-turbo/libjpeg-turbo tag 3.0.4; BSD-style IJG and
# zlib licences). gnustep-gui's NSBitmapImageRep reads and writes JPEG through it (build_gnustep_gui.sh) and
# Milan's Quick Look / wallpaper chooser use that. Apple's ImageIO is closed, so this is the upstream project, as
# build_libtiff.sh and build_libpng.sh.
#
# Recipe (same shape as build_libtiff.sh):
#   config   -- libjpeg-turbo's own CMake configure generates jconfig.h, jconfigint.h and jversion.h (cross target
#               iOS/arm64 on the Xcode 12 SDK, try_compile only, nothing built or run by CMake). No SIMD (WITH_SIMD=0:
#               the NEON code is a later optimisation), no TurboJPEG API, no tools; arithmetic coding on (the default).
#   sources  -- CMakeLists.txt's JPEG_SOURCES / JPEG12_SOURCES / JPEG16_SOURCES / jaricom,jcarith,jdarith. The 12- and
#               16-bit precisions are the same files compiled again with -DBITS_IN_JSAMPLE=12 / 16 (CMakeLists.txt:
#               the jpeg12 / jpeg16 object libraries); their objects are renamed -12 / -16 so all three sets link.
#   version  -- libjpeg.62.dylib (libjpeg API 6.2, what libs-gui's configure looks for with -ljpeg).
set -e
cd "$(dirname "$0")"
. ./fontstack_common.sh

SRC="$TP/libjpeg-turbo"
x11_require_source "$SRC/jpeglib.h"
grep -q 'set(VERSION 3.0.4)' "$SRC/CMakeLists.txt" || { echo "error: libjpeg-turbo is not 3.0.4 -- recheck this script" >&2; exit 1; }
command -v cmake >/dev/null || { echo "error: cmake not found (brew install cmake); the config headers come from libjpeg-turbo's own CMake configure" >&2; exit 1; }
x11_require_sys_dylibs

OBJ="$FS_ROOT/obj/libjpeg"
GEN="$FS_ROOT/gen/libjpeg"
rm -rf "$OBJ" "$OBJ-12" "$OBJ-16" "$GEN"
mkdir -p "$OBJ" "$OBJ-12" "$OBJ-16" "$GEN"
cmake -S "$SRC" -B "$GEN" -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$SDK" -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 -DCMAKE_C_COMPILER="$CLANG" -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DWITH_SIMD=0 -DWITH_TURBOJPEG=0 -DWITH_TOOLS=0 -DWITH_TESTS=0 -DWITH_JAVA=0 -DENABLE_SHARED=0 -DENABLE_STATIC=1 \
    > "$GEN/cmake.out" 2>&1 || { tail -20 "$GEN/cmake.out"; echo "error: libjpeg-turbo cmake configure failed" >&2; exit 1; }
for h in jconfig.h jconfigint.h jversion.h; do
    [ -f "$GEN/$h" ] || { echo "error: cmake did not generate $h" >&2; exit 1; }
done

# the source lists, straight from CMakeLists.txt
lists=$(python3 -I - "$SRC/CMakeLists.txt" <<'PY'
import re, sys
t = open(sys.argv[1]).read()
def files(name):
    m = re.search(r'set\(%s\s+(.*?)\)' % name, t, re.S)
    return [x for x in re.split(r'\s+', m.group(1)) if x.endswith('.c')]
j16 = files('JPEG16_SOURCES')
j12 = files('JPEG12_SOURCES')            # starts with ${JPEG16_SOURCES}; the .c filter drops the reference
j = files('JPEG_SOURCES') + ['jaricom.c', 'jcarith.c', 'jdarith.c']
print('J16="' + ' '.join(j16) + '"')
print('J12="' + ' '.join(j16 + j12) + '"')
print('J8="' + ' '.join(j16 + j12 + j) + '"')
PY
)
eval "$lists"
for f in $J8; do
    x11_compile "$OBJ" "$SRC/$f" -UHAVE_CONFIG_H -I "$GEN" -I "$SRC"
done
for f in $J12; do
    x11_compile "$OBJ-12" "$SRC/$f" -UHAVE_CONFIG_H -DBITS_IN_JSAMPLE=12 -I "$GEN" -I "$SRC"
done
for f in $J16; do
    x11_compile "$OBJ-16" "$SRC/$f" -UHAVE_CONFIG_H -DBITS_IN_JSAMPLE=16 -I "$GEN" -I "$SRC"
done
for o in "$OBJ-12"/*.o; do mv "$o" "$OBJ/$(basename "$o" .o)-12.o"; done
for o in "$OBJ-16"/*.o; do mv "$o" "$OBJ/$(basename "$o" .o)-16.o"; done
x11_compile_report libjpeg

cp "$SRC/jpeglib.h" "$SRC/jerror.h" "$SRC/jmorecfg.h" "$GEN/jconfig.h" "$X11_INC/"

fs_link_dylib libjpeg.dylib "$X11_PREFIX/lib/libjpeg.62.dylib" 62.0.0 62.4.0 "$OBJ"
