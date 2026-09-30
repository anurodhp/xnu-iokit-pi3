#!/bin/bash
# Builds wmclose_probe.macho (DAR-452): a tiny Xlib/XTest client that lists
# Window Maker's managed clients (pid, WM_PROTOCOLS) and drives the exact
# Close (WM_DELETE_WINDOW) and Kill (XKillClient) requests Window Maker
# sends, or clicks a frame's close button through XTest. See
# wmclose_probe.c's header. QEMU-only dev diagnostic: installed at
# /usr/X11/bin/wmclose_probe by inject_into_sd_image.sh via
# install_smoketest (so SKIP_SMOKETESTS drops it from real-HW images).
# Link recipe: build_x11_smoketest.sh's dynamic-executable shape.
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

X11_DYLIBS=(libXtst libXext libX11 libxcb libXdmcp libXau)
for l in "${X11_DYLIBS[@]}"; do
    [ -f "$OUT/system/$l.dylib" ] || { echo "error: $OUT/system/$l.dylib not found -- run build_$l.sh first" >&2; exit 1; }
done
x11_require_sys_dylibs

OBJ="$X11_ROOT/obj/wmclose_probe"
rm -rf "$OBJ"
mkdir -p "$OBJ"
x11_compile "$OBJ" wmclose_probe.c
x11_compile_report wmclose_probe

LINK=()
for l in "${X11_DYLIBS[@]}"; do LINK+=("$OUT/system/$l.dylib"); done
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o wmclose_probe.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OBJ"/*.o "${LINK[@]}" "${X11_SYS_DYLIBS[@]}"
echo "wrote wmclose_probe.macho"

bad=0
otool -l wmclose_probe.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: wmclose_probe: no LC_LOAD_DYLINKER /usr/lib/dyld" >&2; bad=1; }
otool -l wmclose_probe.macho | grep -q "cmd LC_LOAD_DYLIB" || {
    echo "error: wmclose_probe: no LC_LOAD_DYLIB" >&2; bad=1; }
otool -l wmclose_probe.macho | grep -q "cmd LC_MAIN" || {
    echo "error: wmclose_probe: no LC_MAIN" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1
./bind_target_audit.sh wmclose_probe.macho "${LINK[@]}"
