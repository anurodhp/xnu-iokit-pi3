#!/bin/bash
# Builds libc_build/system/libexpat.dylib (install name
# /usr/lib/libexpat.1.dylib) from Apple's own expat (third_party/expat,
# apple-oss-distributions expat-26 -- the macos-112 manifest's pin;
# upstream expat 2.2.8 inside). DAR-384, X11GS 3: fontconfig's XML config
# parser. fontconfig can use libxml2 instead (meson option xml-backend),
# and this tree already has a libxml2 dylib, but upstream's default and
# macOS's own /usr/lib both use expat, so this follows Apple (the
# project's Apple-parity rule) at the cost of three source files.
#
# Recipe, all from Apple's own project files:
#   sources  -- expat.xcodeproj "Sources": xmlparse.c xmlrole.c xmltok.c
#   config   -- Apple's checked-in, configure-generated expat_config.h at
#               the repo root (HEADER_SEARCH_PATHS = $(SRCROOT)), used as is
#   defines  -- GCC_PREPROCESSOR_DEFINITIONS = HAVE_EXPAT_CONFIG_H
#               XML_ENABLE_VISIBILITY=1, plus GCC_SYMBOLS_PRIVATE_EXTERN =
#               YES (-fvisibility=hidden), so only XMLCALL-marked API is
#               exported, as in Apple's build
#   name     -- INSTALL_PATH /usr/lib, PRODUCT_NAME $(TARGET_NAME).1 =
#               libexpat.1.dylib, DYLIB_COMPATIBILITY_VERSION 7,
#               DYLIB_CURRENT_VERSION 8
# Headers expat.h/expat_external.h go to $FS_SYSINC (/usr/include,
# PUBLIC_HEADERS_FOLDER_PATH).
set -e
cd "$(dirname "$0")"
. ./fontstack_common.sh

SRC="$TP/expat"
x11_require_source "$SRC/expat/lib/xmlparse.c"
for d in libsystem_kernel libsystem_c libsystem_pthread libsystem_malloc libsystem_platform libdyld; do
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2; exit 1; }
done
x11_require_sys_dylibs

OBJ="$FS_ROOT/obj/expat"
rm -rf "$OBJ"
mkdir -p "$OBJ" "$FS_SYSINC"
cp "$SRC/expat/lib/expat.h" "$SRC/expat/lib/expat_external.h" "$FS_SYSINC/"
for f in xmlparse xmlrole xmltok; do
    x11_compile "$OBJ" "$SRC/expat/lib/$f.c" -DHAVE_EXPAT_CONFIG_H -DXML_ENABLE_VISIBILITY=1 \
        -fvisibility=hidden -I "$SRC"
done
x11_compile_report expat
fs_link_dylib libexpat.dylib /usr/lib/libexpat.1.dylib 7 8 "$OBJ"
echo "libexpat.dylib: $(nm -gU "$OUT/system/libexpat.dylib" | grep -c ' T _XML_') XML_* functions exported"
