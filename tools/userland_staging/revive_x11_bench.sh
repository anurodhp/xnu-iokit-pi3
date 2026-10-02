#!/bin/bash
# Builds x11_bench.macho (DAR-438): wrlib/Xlib micro-benchmark used to time
# the -O0 vs -O2 X11 stack on the same SD image (see x11_bench.c). Needs
# revive_wmaker.sh (libwraster + its generated wraster.h) first. Installed
# at /usr/X11/bin/x11_bench by inject_into_sd_image.sh.
#
# Link recipe: the same dynamic-executable shape as build_reboot.sh /
# build_netstat.sh (Csu start.s/crt.c with -DCRT_DYNAMIC_LINKING, every
# real owner dylib listed before libSystem.B, the LC_LOAD_DYLINKER/
# LC_LOAD_DYLIB/LC_MAIN assertions), then bind_target_audit.sh and
# gap_audit.sh as hard gates.
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

X11_DYLIBS=(libwraster libpng16 libXmu libXt libSM libICE libXext libX11-xcb libX11
    libxcb-shm libxcb libXdmcp libXau)
if [ ! -f "$OUT/system/libXau.dylib" ]; then
    echo "error: $OUT/system/libXau.dylib not found -- run build_libXau.sh first" >&2; exit 1
fi
if [ ! -f "$OUT/system/libXdmcp.dylib" ]; then
    echo "error: $OUT/system/libXdmcp.dylib not found -- run build_libXdmcp.sh first" >&2; exit 1
fi
if [ ! -f "$OUT/system/libxcb.dylib" ]; then
    echo "error: $OUT/system/libxcb.dylib not found -- run build_libxcb.sh first" >&2; exit 1
fi
if [ ! -f "$OUT/system/libX11.dylib" ]; then
    echo "error: $OUT/system/libX11.dylib not found -- run build_libX11.sh first" >&2; exit 1
fi
if [ ! -f "$OUT/system/libXext.dylib" ]; then
    echo "error: $OUT/system/libXext.dylib not found -- run build_libXext.sh first" >&2; exit 1
fi
if [ ! -f "$OUT/system/libXrender.dylib" ]; then
    echo "error: $OUT/system/libXrender.dylib not found -- run build_libXrender.sh first" >&2; exit 1
fi
if [ ! -f "$OUT/system/libXfixes.dylib" ]; then
    echo "error: $OUT/system/libXfixes.dylib not found -- run build_libXfixes.sh first" >&2; exit 1
fi
if [ ! -f "$OUT/system/libXrandr.dylib" ]; then
    echo "error: $OUT/system/libXrandr.dylib not found -- run build_libXrandr.sh first" >&2; exit 1
fi
if [ ! -f "$OUT/system/libXcursor.dylib" ]; then
    echo "error: $OUT/system/libXcursor.dylib not found -- run build_libXcursor.sh first" >&2; exit 1
fi
if [ ! -f "$OUT/system/libICE.dylib" ]; then
    echo "error: $OUT/system/libICE.dylib not found -- run build_libICE.sh first" >&2; exit 1
fi
if [ ! -f "$OUT/system/libSM.dylib" ]; then
    echo "error: $OUT/system/libSM.dylib not found -- run build_libSM.sh first" >&2; exit 1
fi
if [ ! -f "$OUT/system/libXt.dylib" ]; then
    echo "error: $OUT/system/libXt.dylib not found -- run build_libXt.sh first" >&2; exit 1
fi
if [ ! -f "$OUT/system/libXmu.dylib" ]; then
    echo "error: $OUT/system/libXmu.dylib not found -- run build_libXmu.sh first" >&2; exit 1
fi
for d in libsystem_kernel libsystem_c libsystem_pthread libsystem_malloc libsystem_platform libdyld; do
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2; exit 1; }
done
x11_require_sys_dylibs

OBJ="$X11_ROOT/obj/x11_bench"
rm -rf "$OBJ"
mkdir -p "$OBJ"
x11_compile "$OBJ" x11_bench.c -I "$X11_ROOT/obj/wmaker_gen"
x11_compile_report x11_bench

LINK=()
for l in "${X11_DYLIBS[@]}"; do LINK+=("$OUT/system/$l.dylib"); done
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o x11_bench.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OBJ"/*.o "${LINK[@]}" "${X11_SYS_DYLIBS[@]}"
echo "wrote x11_bench.macho"

bad=0
otool -l x11_bench.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: x11_bench: no LC_LOAD_DYLINKER /usr/lib/dyld" >&2; bad=1; }
otool -l x11_bench.macho | grep -q "cmd LC_LOAD_DYLIB" || {
    echo "error: x11_bench: no LC_LOAD_DYLIB" >&2; bad=1; }
otool -l x11_bench.macho | grep -q "cmd LC_MAIN" || {
    echo "error: x11_bench: no LC_MAIN" >&2; bad=1; }
for l in "${X11_DYLIBS[@]}"; do
    otool -L x11_bench.macho | grep -q "/usr/X11/lib/$l\." || {
        echo "error: x11_bench: $l is not a load-time dependency" >&2; bad=1; }
done
[ "$bad" -eq 0 ] || exit 1

./bind_target_audit.sh x11_bench.macho "${LINK[@]}"

# gap_audit.sh over the whole process image set: every "dynamically
# looked up" (flat-namespace) reference in the executable and each X
# dylib must resolve somewhere in the process. There should be none at
# all -- every link above is two-level.
flat=0
for img in x11_bench.macho "${LINK[@]}"; do
    if nm -m "$img" | grep -q 'dynamically looked up'; then
        echo "error: $img has flat-namespace (dynamically looked up) references:" >&2
        nm -m "$img" | grep 'dynamically looked up' >&2
        flat=1
    fi
done
[ "$flat" -eq 0 ] || exit 1
echo "x11_bench.macho: dynamic-executable assertions OK; no flat-namespace references in it or any X dylib"
