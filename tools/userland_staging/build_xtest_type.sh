#!/bin/bash
# Builds xtest_type.macho: a small XTEST client that types text (plus
# Return) into an X display, used to drive the xdm greeter in QEMU boot
# tests (log in, log out, log in again). Installed on the SD image at
# /usr/X11/bin/xtest_type by inject_into_sd_image.sh.
#
# Same dynamic-executable link recipe and gates as build_x11_smoketest.sh.
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

X11_DYLIBS=(libXtst libXi libXext libX11 libxcb libXdmcp libXau)
for l in "${X11_DYLIBS[@]}"; do
    [ -f "$OUT/system/$l.dylib" ] || { echo "error: $OUT/system/$l.dylib not found -- run build_$l.sh first" >&2; exit 1; }
done
x11_require_sys_dylibs

OBJ="$X11_ROOT/obj/xtest_type"
rm -rf "$OBJ"
mkdir -p "$OBJ"
x11_compile "$OBJ" xtest_type.c
x11_compile_report xtest_type

LINK=()
for l in "${X11_DYLIBS[@]}"; do LINK+=("$OUT/system/$l.dylib"); done
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o xtest_type.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OBJ"/*.o "${LINK[@]}" "${X11_SYS_DYLIBS[@]}"
echo "wrote xtest_type.macho"

bad=0
otool -l xtest_type.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: xtest_type: no LC_LOAD_DYLINKER /usr/lib/dyld" >&2; bad=1; }
otool -l xtest_type.macho | grep -q "cmd LC_MAIN" || {
    echo "error: xtest_type: no LC_MAIN" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1
./bind_target_audit.sh xtest_type.macho "${LINK[@]}"
