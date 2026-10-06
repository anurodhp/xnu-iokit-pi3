#!/bin/bash
# Builds glxinfo and glxgears from Mesa's demos (third_party/mesa-demos,
# tag mesa-demos-9.0.0) against /usr/X11/lib/libGL.1.dylib (build_mesa.sh):
# glxinfo reports the renderer and extensions, glxgears is the usual frame
# rate check (vc4 vs softpipe, Xvfb or the HDMI Xorg). Installed at
# /usr/X11/bin/{glxinfo,glxgears} by inject_into_sd_image.sh.
#
# Unpatched upstream, compiled directly (the few files each needs) instead
# of through the demos' meson build, which wants GLU, GLUT and EGL for its
# other programs. Same dynamic-executable recipe and gates as
# build_gl_smoketest.sh.
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

D="$TP/mesa-demos"
x11_require_source "$D/src/xdemos/glxgears.c"
grep -q "version : '9.0.0'" "$D/meson.build" || { echo "error: mesa-demos is not 9.0.0 -- recheck this script" >&2; exit 1; }
X11_DYLIBS=(libGL libXext libX11-xcb libX11 libxcb-shm libxcb libXdmcp libXau)
for l in "${X11_DYLIBS[@]}"; do
    [ -f "$OUT/system/$l.dylib" ] || { echo "error: $OUT/system/$l.dylib not found -- build it first (libGL: build_mesa.sh)" >&2; exit 1; }
done
x11_require_staged GL/gl.h build_mesa.sh
x11_require_sys_dylibs
LINK=()
for l in "${X11_DYLIBS[@]}"; do LINK+=("$OUT/system/$l.dylib"); done

demo() {
    local name="$1"
    shift
    local obj="$X11_ROOT/obj/mesa_demos_$name"
    rm -rf "$obj"
    mkdir -p "$obj"
    X11_OK=0 X11_FAIL=0
    for src in "$@"; do
        x11_compile "$obj" "$src" -I "$D/src/glad/include" -I "$D/src/util" -Wno-error
    done
    x11_compile_report "$name"
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
        -DCRT_DYNAMIC_LINKING -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
        -o "$name.macho" ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
        "$obj"/*.o "${LINK[@]}" "${X11_SYS_DYLIBS[@]}"
    otool -l "$name.macho" | grep -q "cmd LC_MAIN" || { echo "error: $name: no LC_MAIN" >&2; exit 1; }
    otool -L "$name.macho" | grep -q "/usr/X11/lib/libGL\." || { echo "error: $name: libGL is not a load-time dependency" >&2; exit 1; }
    ./bind_target_audit.sh "$name.macho" "${LINK[@]}"
    if nm -m "$name.macho" | grep -q 'dynamically looked up'; then
        echo "error: $name has flat-namespace references" >&2
        exit 1
    fi
    echo "wrote $name.macho"
}

demo glxgears "$D/src/xdemos/glxgears.c"
demo glxinfo "$D/src/xdemos/glxinfo.c" "$D/src/util/glinfo_common.c" "$D/src/glad/src/glad.c"
