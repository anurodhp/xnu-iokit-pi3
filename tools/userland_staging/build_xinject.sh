#!/bin/bash
# Builds xinject.macho: a tiny Xlib/XTest client that injects real mouse and keyboard input into the X
# server, so GUI apps (Milan, ...) can be driven from a script over ssh. See xinject.c's header.
# Dev tool, not installed on images. Link recipe: build_wmclose_probe.sh.
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

X11_DYLIBS=(libXtst libXext libX11 libxcb libXdmcp libXau)
for l in "${X11_DYLIBS[@]}"; do
    [ -f "$OUT/system/$l.dylib" ] || { echo "error: $OUT/system/$l.dylib not found -- run build_$l.sh first" >&2; exit 1; }
done
x11_require_sys_dylibs

OBJ="$X11_ROOT/obj/xinject"
rm -rf "$OBJ"
mkdir -p "$OBJ"
x11_compile "$OBJ" xinject.c
x11_compile_report xinject

LINK=()
for l in "${X11_DYLIBS[@]}"; do LINK+=("$OUT/system/$l.dylib"); done
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o xinject.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OBJ"/*.o "${LINK[@]}" "${X11_SYS_DYLIBS[@]}"
echo "wrote xinject.macho"

bad=0
otool -l xinject.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: xinject: no LC_LOAD_DYLINKER /usr/lib/dyld" >&2; bad=1; }
otool -l xinject.macho | grep -q "cmd LC_LOAD_DYLIB" || {
    echo "error: xinject: no LC_LOAD_DYLIB" >&2; bad=1; }
otool -l xinject.macho | grep -q "cmd LC_MAIN" || {
    echo "error: xinject: no LC_MAIN" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1
./bind_target_audit.sh xinject.macho "${LINK[@]}"
