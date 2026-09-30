#!/bin/bash
# Builds qos_probe.macho (DAR-439): scheduler priority / QoS probe; see
# qos_probe.c. QEMU-only test aid installed at /usr/X11/bin/qos_probe by
# inject_into_sd_image.sh via install_smoketest. Link recipe: build_wmlat.sh.
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

x11_require_sys_dylibs

OBJ="$X11_ROOT/obj/qos_probe"
rm -rf "$OBJ"
mkdir -p "$OBJ"
x11_compile "$OBJ" qos_probe.c
x11_compile_report qos_probe

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o qos_probe.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OBJ"/*.o "${X11_SYS_DYLIBS[@]}"
echo "wrote qos_probe.macho"

bad=0
otool -l qos_probe.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: qos_probe: no LC_LOAD_DYLINKER /usr/lib/dyld" >&2; bad=1; }
otool -l qos_probe.macho | grep -q "cmd LC_LOAD_DYLIB" || {
    echo "error: qos_probe: no LC_LOAD_DYLIB" >&2; bad=1; }
otool -l qos_probe.macho | grep -q "cmd LC_MAIN" || {
    echo "error: qos_probe: no LC_MAIN" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1
./bind_target_audit.sh qos_probe.macho
