#!/bin/bash
# Builds shm_probe.macho: a dynamic executable that checks each step
# MIT-SHM needs (SysV shmget/shmat, the server's MIT-SHM extension,
# XShmAttach) and prints which one fails (see shm_probe.c). A bring-up
# tool, not installed by inject_into_sd_image.sh: copy it to the target.
#
# Same dynamic-executable link recipe and hard gates as build_x11_bench.sh.
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

X11_DYLIBS=(libXext libX11-xcb libX11 libxcb-shm libxcb libXdmcp libXau)
for l in "${X11_DYLIBS[@]}"; do
    [ -f "$OUT/system/$l.dylib" ] || { echo "error: $OUT/system/$l.dylib not found -- build it first (libGL: build_mesa.sh)" >&2; exit 1; }
done
x11_require_staged X11/extensions/XShm.h build_libXext.sh
x11_require_sys_dylibs

OBJ="$X11_ROOT/obj/shm_probe"
rm -rf "$OBJ"
mkdir -p "$OBJ"
x11_compile "$OBJ" shm_probe.c
x11_compile_report shm_probe

LINK=()
for l in "${X11_DYLIBS[@]}"; do LINK+=("$OUT/system/$l.dylib"); done
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o shm_probe.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OBJ"/*.o "${LINK[@]}" "${X11_SYS_DYLIBS[@]}"
echo "wrote shm_probe.macho"

bad=0
otool -l shm_probe.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: shm_probe: no LC_LOAD_DYLINKER /usr/lib/dyld" >&2; bad=1; }
otool -l shm_probe.macho | grep -q "cmd LC_LOAD_DYLIB" || {
    echo "error: shm_probe: no LC_LOAD_DYLIB" >&2; bad=1; }
otool -l shm_probe.macho | grep -q "cmd LC_MAIN" || {
    echo "error: shm_probe: no LC_MAIN" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1

./bind_target_audit.sh shm_probe.macho "${LINK[@]}"
flat=0
for img in shm_probe.macho "${LINK[@]}"; do
    if nm -m "$img" | grep -q 'dynamically looked up'; then
        echo "error: $img has flat-namespace references:" >&2
        nm -m "$img" | grep 'dynamically looked up' >&2
        flat=1
    fi
done
[ "$flat" -eq 0 ] || exit 1
echo "shm_probe.macho: dynamic-executable assertions OK; no flat-namespace references"
