#!/bin/bash
# libplacebo 7.351.0 (third_party/libplacebo, code.videolan.org/videolan/
# libplacebo tag v7.351.0 with its glad, jinja, markupsafe, fast_float and
# Vulkan-Headers submodules (the Vulkan stubs include vulkan.h even with
# Vulkan off), LGPL-2.1+), the GPU video rendering library libmpv requires
# (build_libmpv.sh): /usr/lib/libplacebo.351.dylib. libmpv's render API
# draws with mpv's own GL renderer (GL 2.1 is enough); libplacebo's GL
# backend, which needs GL 3, is only for vo=gpu-next, which Venice does not
# use. libmpv links it for its shader and colour utilities.
#
# OpenGL backend only: Mesa's libGL (GL 2.1 on the Pi's VC4 V3D, softpipe
# elsewhere). The GL entry points come from the context's
# glXGetProcAddress, which libmpv hands over (-Dgl-proc-addr=disabled: no
# dlopen of a GL library by name). No Vulkan, D3D11, glslang/shaderc
# (GLSL goes to the driver as text), LittleCMS or Dolby Vision. Unmodified
# upstream, its own meson build, media_common.sh's recipe.
set -euo pipefail
cd "$(dirname "$0")"
. ./media_common.sh
# media_common.sh links against these; for gen_build_order.py, which reads only build_*.sh:
# run build_libsystem.sh build_libinfo.sh build_libresolv_dylib.sh build_libm_dylib.sh build_libcxx_dylib.sh build_zlib.sh build_freetype.sh build_fontconfig.sh first
SRC="$PWD/$TP/libplacebo"
x11_require_source "$SRC/meson.build"
[ -f "$SRC/3rdparty/glad/glad/__init__.py" ] && [ -d "$SRC/3rdparty/jinja/src" ] && [ -f "$SRC/3rdparty/Vulkan-Headers/include/vulkan/vulkan.h" ] ||
    { echo "error: libplacebo's glad/jinja/Vulkan-Headers submodules are missing -- run setup_third_party.sh" >&2; exit 1; }
# the newest entry of meson.build's API-version table is the release's
[ "$(grep -m1 -oE "^      '[0-9]+':" "$SRC/meson.build")" = "      '351':" ] ||
    { echo "error: libplacebo is not 7.351 -- recheck this script" >&2; exit 1; }
media_meson libplacebo "$SRC" --default-library=shared -Dvulkan=disabled -Dopengl=enabled \
    -Dgl-proc-addr=disabled -Dd3d11=disabled -Dglslang=disabled -Dshaderc=disabled -Dlcms=disabled \
    -Ddovi=disabled -Dlibdovi=disabled -Ddemos=false -Dtests=false -Dbench=false -Dfuzz=false \
    -Dunwind=disabled -Dxxhash=disabled
media_install_dylib "$MEDIA_GEN/libplacebo/src/libplacebo.351.dylib" libplacebo.dylib /usr/lib/libplacebo.351.dylib
media_stage_headers libplacebo
media_pc libplacebo 7.351.0 "-I$MEDIA_INC" "$MEDIA_SYS/libplacebo.dylib"
