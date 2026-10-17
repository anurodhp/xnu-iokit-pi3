#!/bin/bash
# fribidi 1.0.16 (third_party/fribidi, github.com/fribidi/fribidi tag v1.0.16,
# LGPL-2.1+), the Unicode bidirectional algorithm libass uses for subtitles
# (build_libass.sh): /usr/lib/libfribidi.0.dylib. Unmodified upstream, built
# with its own meson build (library only) by media_common.sh's recipe.
set -euo pipefail
cd "$(dirname "$0")"
. ./media_common.sh
# media_common.sh links against these; for gen_build_order.py, which reads only build_*.sh:
# run build_libsystem.sh build_libinfo.sh build_libresolv_dylib.sh build_libm_dylib.sh build_libcxx_dylib.sh build_zlib.sh build_freetype.sh build_fontconfig.sh first
SRC="$PWD/$TP/fribidi"
x11_require_source "$SRC/meson.build"
grep -q "version: '1.0.16'" "$SRC/meson.build" || { echo "error: fribidi is not 1.0.16 -- recheck this script" >&2; exit 1; }
media_meson fribidi "$SRC" --default-library=shared -Dbin=false -Ddocs=false -Dtests=false
media_install_dylib "$MEDIA_GEN/fribidi/lib/libfribidi.0.dylib" libfribidi.dylib /usr/lib/libfribidi.0.dylib
rm -rf "$MEDIA_INC/fribidi"; mkdir -p "$MEDIA_INC/fribidi"
cp "$SRC"/lib/fribidi*.h "$MEDIA_GEN"/fribidi/lib/fribidi-config.h "$MEDIA_GEN"/fribidi/gen.tab/fribidi-unicode-version.h "$MEDIA_INC/fribidi/"
media_pc fribidi 1.0.16 "-I$MEDIA_INC/fribidi" "$MEDIA_SYS/libfribidi.dylib"
