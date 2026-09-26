#!/bin/bash
# Builds libc_build/system/libz.dylib (install name /usr/lib/libz.1.dylib)
# from Apple's own zlib (third_party/zlib, apple-oss-distributions
# zlib-76 -- the macos-112 manifest's pin; upstream zlib 1.2.11 inside).
# DAR-384, X11GS 3: libpng, freetype and cairo need a real zlib dylib, and
# until now this tree only had u-boot's vendored deflate/inflate core
# compiled into individual commands (build_simple_cmds.sh's zlib_glue),
# with no gz*/compress/uncompress and no shared library.
#
# Recipe, all from Apple's own project files:
#   sources  -- zlib.xcodeproj's libz target "Sources" phase: adler32
#               compress crc32 deflate gzclose gzlib gzread gzwrite
#               infback inffast inflate inftrees trees uncompr zutil
#               (t_zlib.c there belongs to the test target, not libz)
#   defines  -- zlib.xcconfig: GCC_PREPROCESSOR_DEFINITIONS = USE_MMAP
#               VEC_OPTIMIZE (neither is referenced by 1.2.11's own
#               sources; passed anyway so the compile matches Apple's)
#   exports  -- zlib.xcconfig: EXPORTED_SYMBOLS_FILE = libz.exp
#   name     -- zlib.xcconfig: INSTALL_PATH /usr/lib, PRODUCT_NAME
#               z.$(DYLIB_COMPATIBILITY_VERSION) = libz.1.dylib,
#               DYLIB_COMPATIBILITY_VERSION 1, DYLIB_CURRENT_VERSION 1.2.11
# Headers zlib.h/zconf.h are staged into $FS_SYSINC (/usr/include), the
# xcconfig's PUBLIC_HEADERS_FOLDER_PATH.
set -e
cd "$(dirname "$0")"
. ./fontstack_common.sh

SRC="$TP/zlib"
x11_require_source "$SRC/zlib/zlib.h"
for d in libsystem_kernel libsystem_c libsystem_pthread libsystem_malloc libsystem_platform libdyld; do
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2; exit 1; }
done
x11_require_sys_dylibs

OBJ="$FS_ROOT/obj/zlib"
rm -rf "$OBJ"
mkdir -p "$OBJ" "$FS_SYSINC"
cp "$SRC/zlib/zlib.h" "$SRC/zlib/zconf.h" "$FS_SYSINC/"
for f in adler32 compress crc32 deflate gzclose gzlib gzread gzwrite \
         infback inffast inflate inftrees trees uncompr zutil; do
    x11_compile "$OBJ" "$SRC/zlib/$f.c" -DUSE_MMAP -DVEC_OPTIMIZE -I "$SRC/zlib"
done
x11_compile_report zlib
fs_link_dylib libz.dylib /usr/lib/libz.1.dylib 1 1.2.11 "$OBJ" \
    -Wl,-exported_symbols_list,"$SRC/libz.exp"
n=$(nm -gU "$OUT/system/libz.dylib" | grep -c ' T ')
echo "libz.dylib: $n exported functions (libz.exp lists $(grep -c . "$SRC/libz.exp"))"
