#!/bin/bash
# libass 0.17.4 (third_party/libass, github.com/libass/libass tag 0.17.4,
# ISC), the subtitle renderer libmpv requires (build_libmpv.sh):
# /usr/lib/libass.9.dylib, on FreeType, fribidi, HarfBuzz and fontconfig
# (the system font provider; CoreText is not on this system). The aarch64
# NEON rasteriser is on. Unmodified upstream, its own meson build,
# media_common.sh's recipe.
set -euo pipefail
cd "$(dirname "$0")"
. ./media_common.sh
# media_common.sh links against these; for gen_build_order.py, which reads only build_*.sh:
# run build_libsystem.sh build_libinfo.sh build_libresolv_dylib.sh build_libm_dylib.sh build_libcxx_dylib.sh build_zlib.sh build_freetype.sh build_fontconfig.sh first
SRC="$PWD/$TP/libass"
x11_require_source "$SRC/meson.build"
grep -qx "0.17.4" "$SRC/RELEASEVERSION" || { echo "error: libass is not 0.17.4 -- recheck this script" >&2; exit 1; }
[ -f "$MEDIA_PC/fribidi.pc" ] || { echo "error: fribidi not built -- run build_fribidi.sh first" >&2; exit 1; }
[ -f "$MEDIA_PC/harfbuzz.pc" ] || { echo "error: harfbuzz not built -- run build_harfbuzz.sh first" >&2; exit 1; }
media_meson libass "$SRC" --default-library=shared -Dfontconfig=enabled -Dcoretext=disabled \
    -Ddirectwrite=disabled -Dlibunibreak=disabled -Dasm=enabled -Dtest=disabled -Dcompare=disabled \
    -Dprofile=disabled -Dfuzz=disabled -Dcheckasm=disabled
media_install_dylib "$MEDIA_GEN/libass/libass/libass.9.dylib" libass.dylib /usr/lib/libass.9.dylib
media_stage_headers libass
media_pc libass 0.17.4 "-I$MEDIA_INC" "$MEDIA_SYS/libass.dylib" "freetype2 fribidi harfbuzz fontconfig"
