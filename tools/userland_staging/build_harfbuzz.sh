#!/bin/bash
# HarfBuzz 11.2.1 (third_party/harfbuzz, github.com/harfbuzz/harfbuzz tag
# 11.2.1, MIT), the text shaper libass needs (build_libass.sh):
# /usr/lib/libharfbuzz.0.dylib, on FreeType (the staged /usr/X11 one). No
# glib, ICU, cairo, GObject or subsetter; C++ against this project's libc++.
# Unmodified upstream, its own meson build, media_common.sh's recipe.
set -euo pipefail
cd "$(dirname "$0")"
. ./media_common.sh
# media_common.sh links against these; for gen_build_order.py, which reads only build_*.sh:
# run build_libsystem.sh build_libinfo.sh build_libresolv_dylib.sh build_libm_dylib.sh build_libcxx_dylib.sh build_zlib.sh build_freetype.sh build_fontconfig.sh first
SRC="$PWD/$TP/harfbuzz"
x11_require_source "$SRC/meson.build"
grep -q "version: '11.2.1'" "$SRC/meson.build" || { echo "error: harfbuzz is not 11.2.1 -- recheck this script" >&2; exit 1; }
media_meson harfbuzz "$SRC" --default-library=shared -Dfreetype=enabled -Dglib=disabled -Dgobject=disabled \
    -Dcairo=disabled -Dicu=disabled -Dgraphite2=disabled -Dchafa=disabled -Dintrospection=disabled \
    -Dtests=disabled -Ddocs=disabled -Dbenchmark=disabled -Dutilities=disabled
media_install_dylib "$MEDIA_GEN/harfbuzz/src/libharfbuzz.0.dylib" libharfbuzz.dylib /usr/lib/libharfbuzz.0.dylib
media_stage_headers harfbuzz
media_pc harfbuzz 11.2.1 "-I$MEDIA_INC/harfbuzz" "$MEDIA_SYS/libharfbuzz.dylib" "freetype2"
