#!/bin/bash
# Builds dispatch_stall_test.macho (DAR-409): functional test of libdispatch's
# thread-pool stall monitor; see dispatch_stall_test.c. A QEMU/real-Pi test
# aid, installed at /usr/X11/bin/dispatch_stall_test by inject_into_sd_image.sh
# via install_smoketest. Dynamic link, recipe as build_wutil_timer_test.sh.
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

[ -f "$OUT/system/libdispatch.dylib" ] || { echo "error: $OUT/system/libdispatch.dylib not found -- run build_libdispatch_real.sh + build_libdispatch_dylib.sh first" >&2; exit 1; }
x11_require_sys_dylibs

OBJ="$X11_ROOT/obj/dispatch_stall_test"
rm -rf "$OBJ"
mkdir -p "$OBJ"
x11_compile "$OBJ" dispatch_stall_test.c -fblocks
x11_compile_report dispatch_stall_test

# libdispatch.dylib first: it reexports libsystem_blocks/_c/_kernel and the
# bind_target_audit below checks the result.
LINK=("$OUT/system/libdispatch.dylib")
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o dispatch_stall_test.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OBJ"/*.o "${LINK[@]}" "${X11_SYS_DYLIBS[@]}"
echo "wrote dispatch_stall_test.macho"

bad=0
otool -l dispatch_stall_test.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: dispatch_stall_test: no LC_LOAD_DYLINKER /usr/lib/dyld" >&2; bad=1; }
otool -l dispatch_stall_test.macho | grep -q "cmd LC_LOAD_DYLIB" || {
    echo "error: dispatch_stall_test: no LC_LOAD_DYLIB" >&2; bad=1; }
otool -l dispatch_stall_test.macho | grep -q "cmd LC_MAIN" || {
    echo "error: dispatch_stall_test: no LC_MAIN" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1
./bind_target_audit.sh dispatch_stall_test.macho "${LINK[@]}"
