#!/bin/bash
# Builds wmlat.macho (DAR-431): Window Maker event-loop latency probe; see
# wmlat.c. QEMU-only test aid installed at /usr/X11/bin/wmlat by
# inject_into_sd_image.sh via install_smoketest. Link recipe: build_x11_probe.sh.
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

X11_DYLIBS=(libXtst libXext libX11 libxcb libXdmcp libXau)
for l in "${X11_DYLIBS[@]}"; do
    [ -f "$OUT/system/$l.dylib" ] || { echo "error: $OUT/system/$l.dylib not found -- run build_$l.sh first" >&2; exit 1; }
done
x11_require_sys_dylibs

OBJ="$X11_ROOT/obj/wmlat"
rm -rf "$OBJ"
mkdir -p "$OBJ"
x11_compile "$OBJ" wmlat.c
x11_compile_report wmlat

LINK=()
for l in "${X11_DYLIBS[@]}"; do LINK+=("$OUT/system/$l.dylib"); done
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o wmlat.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OBJ"/*.o "${LINK[@]}" "${X11_SYS_DYLIBS[@]}"
echo "wrote wmlat.macho"

bad=0
otool -l wmlat.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: wmlat: no LC_LOAD_DYLINKER /usr/lib/dyld" >&2; bad=1; }
otool -l wmlat.macho | grep -q "cmd LC_LOAD_DYLIB" || {
    echo "error: wmlat: no LC_LOAD_DYLIB" >&2; bad=1; }
otool -l wmlat.macho | grep -q "cmd LC_MAIN" || {
    echo "error: wmlat: no LC_MAIN" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1
./bind_target_audit.sh wmlat.macho "${LINK[@]}"
