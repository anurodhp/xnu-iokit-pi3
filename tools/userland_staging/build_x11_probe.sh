#!/bin/bash
# Builds x11_probe.macho (DAR-451): a small X client (window tree + XTest
# clicks) for checking a GUI app from the serial shell; see x11_probe.c's
# header. Not installed by inject_into_sd_image.sh -- a test aid, copied
# onto a test image by hand.
#
# Link recipe: build_x11_smoketest.sh's dynamic-executable shape (Csu
# start.s/crt.c with -DCRT_DYNAMIC_LINKING, owner dylibs before
# libSystem.B), then the LC_LOAD_DYLINKER/LC_LOAD_DYLIB/LC_MAIN assertions
# and bind_target_audit.sh as a hard gate.
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

X11_DYLIBS=(libXtst libXrender libXext libX11 libxcb libXdmcp libXau)
for l in "${X11_DYLIBS[@]}"; do
    [ -f "$OUT/system/$l.dylib" ] || { echo "error: $OUT/system/$l.dylib not found -- run build_$l.sh first" >&2; exit 1; }
done
x11_require_sys_dylibs

OBJ="$X11_ROOT/obj/x11_probe"
rm -rf "$OBJ"
mkdir -p "$OBJ"
x11_compile "$OBJ" x11_probe.c
x11_compile_report x11_probe

LINK=()
for l in "${X11_DYLIBS[@]}"; do LINK+=("$OUT/system/$l.dylib"); done
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o x11_probe.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OBJ"/*.o "${LINK[@]}" "${X11_SYS_DYLIBS[@]}"
echo "wrote x11_probe.macho"

bad=0
otool -l x11_probe.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: x11_probe: no LC_LOAD_DYLINKER /usr/lib/dyld" >&2; bad=1; }
otool -l x11_probe.macho | grep -q "cmd LC_LOAD_DYLIB" || {
    echo "error: x11_probe: no LC_LOAD_DYLIB" >&2; bad=1; }
otool -l x11_probe.macho | grep -q "cmd LC_MAIN" || {
    echo "error: x11_probe: no LC_MAIN" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1
./bind_target_audit.sh x11_probe.macho "${LINK[@]}"
