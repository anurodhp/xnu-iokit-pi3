#!/bin/bash
# Builds libc_build/system/libtiff.dylib (install name
# /usr/X11/lib/libtiff.6.dylib) from real, unmodified libtiff 4.7.1
# (third_party/libtiff, gitlab.com/libtiff/libtiff tag v4.7.1). DAR-390,
# X11GS 6: gnustep-gui's NSBitmapImageRep requires it
# (libs-gui configure.ac:307, "gnustep-gui will not compile without tiff").
# Apple never open-sourced a libtiff (ImageIO is closed), so this is the
# upstream project. It sits in /usr/X11/lib beside libpng16 and the rest
# of the 2D stack (fontstack_common.sh's header), which is where its
# only client finds its headers too.
#
# Recipe:
#   config    -- libtiff's own CMake configure generates tif_config.h,
#                tiffconf.h and tiffvers.h (cross target iOS/arm64 on the
#                Xcode 12 SDK, try_compile only, nothing built or run by
#                CMake). Codecs: libtiff's defaults (CCITT, PackBits, LZW,
#                ThunderScan, NeXT, LogLuv, MDI) plus zlib (ZIP and
#                PixarLog) against build_zlib.sh's libz. JPEG, JBIG, LERC,
#                LZMA, ZSTD, WebP and libdeflate are off: none is built here.
#   sources   -- libtiff/Makefile.am:60-112 libtiff_la_SOURCES with the
#                tif_unix.c branch (:112); the C++ tif_stream.cxx is not
#                built (gnustep-gui uses the C API only). Codec files whose
#                *_SUPPORT is off compile to nothing, as upstream.
#   version   -- configure.ac:80-82 LIBTIFF_CURRENT=8 REVISION=0 AGE=2:
#                libtiff.6.dylib, compat 9, current 9.0.
set -e
cd "$(dirname "$0")"
. ./fontstack_common.sh

SRC="$TP/libtiff"
x11_require_source "$SRC/libtiff/Makefile.am"
grep -q '^LIBTIFF_MICRO_VERSION=1$' "$SRC/configure.ac" && grep -q '^LIBTIFF_MINOR_VERSION=7$' "$SRC/configure.ac" ||
    { echo "error: libtiff is not 4.7.1 -- recheck this script" >&2; exit 1; }
[ -f "$OUT/system/libz.dylib" ] && [ -f "$FS_SYSINC/zlib.h" ] || { echo "error: zlib not built -- run build_zlib.sh first" >&2; exit 1; }
command -v cmake >/dev/null || { echo "error: cmake not found (brew install cmake); libtiff's config headers come from its own CMake configure" >&2; exit 1; }
x11_require_sys_dylibs

OBJ="$FS_ROOT/obj/libtiff"
GEN="$FS_ROOT/gen/libtiff"
rm -rf "$OBJ" "$GEN"
mkdir -p "$OBJ" "$GEN"
cmake -S "$SRC" -B "$GEN" -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$SDK" -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_C_COMPILER="$CLANG" -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -Djpeg=OFF -Dold-jpeg=OFF -Djbig=OFF -Dlerc=OFF -Dlzma=OFF -Dzstd=OFF -Dwebp=OFF -Dlibdeflate=OFF \
    -Dtiff-tools=OFF -Dtiff-tests=OFF -Dtiff-contrib=OFF -Dtiff-docs=OFF -Dtiff-opengl=OFF \
    -DZLIB_INCLUDE_DIR="$PWD/$FS_SYSINC" -DZLIB_LIBRARY="$PWD/$OUT/system/libz.dylib" > "$GEN/cmake.out" 2>&1 ||
    { tail -20 "$GEN/cmake.out"; echo "error: libtiff cmake configure failed" >&2; exit 1; }
for h in tif_config.h tiffconf.h tiffvers.h; do
    [ -f "$GEN/libtiff/$h" ] || { echo "error: cmake did not generate $h" >&2; exit 1; }
done
grep -q '^#define ZIP_SUPPORT 1' "$GEN/libtiff/tiffconf.h" || { echo "error: zlib (ZIP_SUPPORT) not enabled -- see $GEN/cmake.out" >&2; exit 1; }

SRCS=$(x11_am_sources "$SRC/libtiff/Makefile.am" libtiff_la_SOURCES | grep '\.c$' | grep -v tif_win32.c)
echo "$SRCS" | grep -q '^tif_unix.c$' || SRCS="$SRCS
tif_unix.c"
for f in $SRCS; do
    # -UHAVE_CONFIG_H: x11_common.sh's flag is for autoconf config.h;
    # libtiff includes tif_config.h unconditionally (tiffiop.h:30).
    x11_compile "$OBJ" "$SRC/libtiff/$f" -UHAVE_CONFIG_H -I "$GEN/libtiff" -I "$SRC/libtiff"
done
x11_compile_report libtiff

cp "$SRC/libtiff/tiff.h" "$SRC/libtiff/tiffio.h" "$GEN/libtiff/tiffconf.h" "$GEN/libtiff/tiffvers.h" "$X11_INC/"

read -r major compat current <<< "$(fs_version_info 8:0:2)"
fs_link_dylib libtiff.dylib "$X11_PREFIX/lib/libtiff.$major.dylib" "$compat" "$current" "$OBJ" \
    "$OUT/system/libz.dylib"
