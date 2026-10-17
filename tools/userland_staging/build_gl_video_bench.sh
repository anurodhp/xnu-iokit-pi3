#!/bin/bash
# Builds gl_video_bench.macho (/usr/X11/bin/gl_video_bench): a video
# player's GL frame (three YUV plane uploads, a YUV->RGB quad, swap) timed
# step by step against a running X server (see gl_video_bench.c).
#
# Same dynamic-executable link recipe and hard gates as build_gl_smoketest.sh.
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

X11_DYLIBS=(libGL libXext libX11-xcb libX11 libxcb-shm libxcb libXdmcp libXau)
for l in "${X11_DYLIBS[@]}"; do
    [ -f "$OUT/system/$l.dylib" ] || { echo "error: $OUT/system/$l.dylib not found -- build it first (libGL: build_mesa.sh)" >&2; exit 1; }
done
x11_require_staged GL/gl.h build_mesa.sh
x11_require_sys_dylibs

OBJ="$X11_ROOT/obj/gl_video_bench"
rm -rf "$OBJ"
mkdir -p "$OBJ"
x11_compile "$OBJ" gl_video_bench.c
x11_compile_report gl_video_bench

LINK=()
for l in "${X11_DYLIBS[@]}"; do LINK+=("$OUT/system/$l.dylib"); done
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o gl_video_bench.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OBJ"/*.o "${LINK[@]}" "${X11_SYS_DYLIBS[@]}"
echo "wrote gl_video_bench.macho"

bad=0
otool -l gl_video_bench.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: gl_video_bench: no LC_LOAD_DYLINKER /usr/lib/dyld" >&2; bad=1; }
otool -l gl_video_bench.macho | grep -q "cmd LC_LOAD_DYLIB" || {
    echo "error: gl_video_bench: no LC_LOAD_DYLIB" >&2; bad=1; }
otool -l gl_video_bench.macho | grep -q "cmd LC_MAIN" || {
    echo "error: gl_video_bench: no LC_MAIN" >&2; bad=1; }
otool -L gl_video_bench.macho | grep -q "/usr/X11/lib/libGL\." || {
    echo "error: gl_video_bench: libGL is not a load-time dependency" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1

./bind_target_audit.sh gl_video_bench.macho "${LINK[@]}"
flat=0
for img in gl_video_bench.macho "${LINK[@]}"; do
    if nm -m "$img" | grep -q 'dynamically looked up'; then
        echo "error: $img has flat-namespace references:" >&2
        nm -m "$img" | grep 'dynamically looked up' >&2
        flat=1
    fi
done
[ "$flat" -eq 0 ] || exit 1
echo "gl_video_bench.macho: dynamic-executable assertions OK; no flat-namespace references"
