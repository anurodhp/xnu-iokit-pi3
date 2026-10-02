#!/bin/bash
# Builds wutil_timer_test.macho (DAR-432): functional test of libWUtil's
# timer queue across wall-clock steps and leeway coalescing; see wutil_timer_test.c. A QEMU-only test aid,
# installed at /usr/X11/bin/wutil_timer_test by inject_into_sd_image.sh via
# install_smoketest. Link recipe: build_x11_probe.sh's.
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

X11_DYLIBS=(libWUtil)
for l in "${X11_DYLIBS[@]}"; do
    [ -f "$OUT/system/$l.dylib" ] || { echo "error: $OUT/system/$l.dylib not found -- run revive_wmaker.sh first" >&2; exit 1; }
done
x11_require_sys_dylibs

OBJ="$X11_ROOT/obj/wutil_timer_test"
rm -rf "$OBJ"
mkdir -p "$OBJ"
x11_compile "$OBJ" wutil_timer_test.c
x11_compile_report wutil_timer_test

LINK=()
for l in "${X11_DYLIBS[@]}"; do LINK+=("$OUT/system/$l.dylib"); done
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o wutil_timer_test.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OBJ"/*.o "${LINK[@]}" "${X11_SYS_DYLIBS[@]}"
echo "wrote wutil_timer_test.macho"

bad=0
otool -l wutil_timer_test.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: wutil_timer_test: no LC_LOAD_DYLINKER /usr/lib/dyld" >&2; bad=1; }
otool -l wutil_timer_test.macho | grep -q "cmd LC_LOAD_DYLIB" || {
    echo "error: wutil_timer_test: no LC_LOAD_DYLIB" >&2; bad=1; }
otool -l wutil_timer_test.macho | grep -q "cmd LC_MAIN" || {
    echo "error: wutil_timer_test: no LC_MAIN" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1
./bind_target_audit.sh wutil_timer_test.macho "${LINK[@]}"
