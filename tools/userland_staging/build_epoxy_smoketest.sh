#!/bin/bash
# Builds epoxy_smoketest.macho: a dynamic executable that links
# /usr/X11/lib/libepoxy.0.dylib (build_libepoxy.sh) and exercises GLX + fixed
# function + GLSL against a running X server (see epoxy_smoketest.c). Installed
# at /usr/X11/bin/epoxy_smoketest by inject_into_sd_image.sh.
#
# Same dynamic-executable link recipe and hard gates as build_x11_bench.sh.
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

X11_DYLIBS=(libepoxy libXext libX11-xcb libX11 libxcb-shm libxcb libXdmcp libXau)
for l in "${X11_DYLIBS[@]}"; do
    [ -f "$OUT/system/$l.dylib" ] || { echo "error: $OUT/system/$l.dylib not found -- build it first (libGL: build_mesa.sh)" >&2; exit 1; }
done
x11_require_staged epoxy/gl.h build_libepoxy.sh
x11_require_sys_dylibs

OBJ="$X11_ROOT/obj/epoxy_smoketest"
rm -rf "$OBJ"
mkdir -p "$OBJ"
x11_compile "$OBJ" epoxy_smoketest.c
x11_compile_report epoxy_smoketest

LINK=()
for l in "${X11_DYLIBS[@]}"; do LINK+=("$OUT/system/$l.dylib"); done
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o epoxy_smoketest.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OBJ"/*.o "${LINK[@]}" "${X11_SYS_DYLIBS[@]}"
echo "wrote epoxy_smoketest.macho"

bad=0
otool -l epoxy_smoketest.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: epoxy_smoketest: no LC_LOAD_DYLINKER /usr/lib/dyld" >&2; bad=1; }
otool -l epoxy_smoketest.macho | grep -q "cmd LC_LOAD_DYLIB" || {
    echo "error: epoxy_smoketest: no LC_LOAD_DYLIB" >&2; bad=1; }
otool -l epoxy_smoketest.macho | grep -q "cmd LC_MAIN" || {
    echo "error: epoxy_smoketest: no LC_MAIN" >&2; bad=1; }
otool -L epoxy_smoketest.macho | grep -q "/usr/X11/lib/libepoxy\." || {
    echo "error: epoxy_smoketest: libepoxy is not a load-time dependency" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1

./bind_target_audit.sh epoxy_smoketest.macho "${LINK[@]}"
flat=0
for img in epoxy_smoketest.macho "${LINK[@]}"; do
    if nm -m "$img" | grep -q 'dynamically looked up'; then
        echo "error: $img has flat-namespace references:" >&2
        nm -m "$img" | grep 'dynamically looked up' >&2
        flat=1
    fi
done
[ "$flat" -eq 0 ] || exit 1
echo "epoxy_smoketest.macho: dynamic-executable assertions OK; no flat-namespace references"
