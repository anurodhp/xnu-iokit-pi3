#!/bin/bash
# Builds v3d_membench.macho: bring-up tool for the BCM2837V3D kext (see
# v3d_membench.c): CPU bandwidth of a V3D BO mapping vs ordinary memory.
# Not installed by inject_into_sd_image.sh: copy it to the target.
# Same dynamic-executable recipe and gates as build_x11_bench.sh.
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

LIBS=(libdrm libiokituser libCoreFoundation)
for l in "${LIBS[@]}"; do
    [ -f "$OUT/system/$l.dylib" ] || { echo "error: $OUT/system/$l.dylib not found -- build it first (libdrm: build_libdrm_v3d.sh)" >&2; exit 1; }
done
x11_require_sys_dylibs

OBJ="$X11_ROOT/obj/v3d_membench"
rm -rf "$OBJ"
mkdir -p "$OBJ"
x11_compile "$OBJ" v3d_membench.c -I v3d_drm -I v3d_drm/include -I ../../iokit/BCM2837V3D -I "$TP/mesa/include" -DPRIVATE=1
x11_compile_report v3d_membench

LINK=()
for l in "${LIBS[@]}"; do LINK+=("$OUT/system/$l.dylib"); done
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o v3d_membench.macho \
    "$TP/Csu/start.s" "$TP/Csu/crt.c" \
    "$OBJ"/*.o "${LINK[@]}" "${X11_SYS_DYLIBS[@]}"
echo "wrote v3d_membench.macho"

bad=0
otool -l v3d_membench.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || { echo "error: v3d_membench: no LC_LOAD_DYLINKER" >&2; bad=1; }
otool -l v3d_membench.macho | grep -q "cmd LC_LOAD_DYLIB" || { echo "error: v3d_membench: no LC_LOAD_DYLIB" >&2; bad=1; }
otool -l v3d_membench.macho | grep -q "cmd LC_MAIN" || { echo "error: v3d_membench: no LC_MAIN" >&2; bad=1; }
otool -L v3d_membench.macho | grep -q "/usr/X11/lib/libdrm\." || { echo "error: v3d_membench: libdrm is not a load-time dependency" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1
./bind_target_audit.sh v3d_membench.macho "${LINK[@]}"
for img in v3d_membench.macho "${LINK[@]}"; do
    if nm -m "$img" | grep -q 'dynamically looked up'; then
        echo "error: $img has flat-namespace references:" >&2
        nm -m "$img" | grep 'dynamically looked up' >&2
        exit 1
    fi
done
echo "v3d_membench.macho: dynamic-executable assertions OK"
