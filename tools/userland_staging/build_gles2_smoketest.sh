#!/bin/bash
# Builds gles2_smoketest.macho: a dynamic executable that links libGLESv2 +
# /usr/X11/lib/libGL.1.dylib (build_mesa.sh) and exercises GLX + fixed
# function + GLSL against a running X server (see gles2_smoketest.c). Installed
# at /usr/X11/bin/gles2_smoketest by inject_into_sd_image.sh.
#
# Same dynamic-executable link recipe and hard gates as build_x11_bench.sh.
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

X11_DYLIBS=(libGLESv2 libGL libXext libX11-xcb libX11 libxcb-shm libxcb libXdmcp libXau)
for l in "${X11_DYLIBS[@]}"; do
    [ -f "$OUT/system/$l.dylib" ] || { echo "error: $OUT/system/$l.dylib not found -- build it first (libGL: build_mesa.sh)" >&2; exit 1; }
done
x11_require_staged GL/gl.h build_mesa.sh
x11_require_sys_dylibs

OBJ="$X11_ROOT/obj/gles2_smoketest"
rm -rf "$OBJ"
mkdir -p "$OBJ"
x11_compile "$OBJ" gles2_smoketest.c
x11_compile_report gles2_smoketest

LINK=()
for l in "${X11_DYLIBS[@]}"; do LINK+=("$OUT/system/$l.dylib"); done
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o gles2_smoketest.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OBJ"/*.o "${LINK[@]}" "${X11_SYS_DYLIBS[@]}"
echo "wrote gles2_smoketest.macho"

bad=0
otool -l gles2_smoketest.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: gles2_smoketest: no LC_LOAD_DYLINKER /usr/lib/dyld" >&2; bad=1; }
otool -l gles2_smoketest.macho | grep -q "cmd LC_LOAD_DYLIB" || {
    echo "error: gles2_smoketest: no LC_LOAD_DYLIB" >&2; bad=1; }
otool -l gles2_smoketest.macho | grep -q "cmd LC_MAIN" || {
    echo "error: gles2_smoketest: no LC_MAIN" >&2; bad=1; }
otool -L gles2_smoketest.macho | grep -q "/usr/X11/lib/libGL\." || {
    echo "error: gles2_smoketest: libGL is not a load-time dependency" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1

./bind_target_audit.sh gles2_smoketest.macho "${LINK[@]}"
flat=0
for img in gles2_smoketest.macho "${LINK[@]}"; do
    if nm -m "$img" | grep -q 'dynamically looked up'; then
        echo "error: $img has flat-namespace references:" >&2
        nm -m "$img" | grep 'dynamically looked up' >&2
        flat=1
    fi
done
[ "$flat" -eq 0 ] || exit 1
echo "gles2_smoketest.macho: dynamic-executable assertions OK; no flat-namespace references"
