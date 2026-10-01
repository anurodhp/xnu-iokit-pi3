#!/bin/bash
# Builds spawn_test.macho (DAR-434): posix_spawn(p) attribute and EVFILT_PROC
# exit-notification test; see spawn_test.c. A QEMU/real-Pi test aid, installed
# at /usr/X11/bin/spawn_test by inject_into_sd_image.sh via install_smoketest.
# Needs libsystem_c.dylib's posix_spawnp (build_libsystem_c.sh). Dynamic link,
# recipe as build_dispatch_stall_test.sh.
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

[ -f "$OUT/system/libsystem_c.dylib" ] || { echo "error: $OUT/system/libsystem_c.dylib not found -- run build_libsystem_c.sh first" >&2; exit 1; }
xcrun dyld_info -exports "$OUT/system/libsystem_c.dylib" | grep -q ' _posix_spawnp$' || { echo "error: libsystem_c.dylib does not export posix_spawnp -- rerun build_libsystem_c.sh" >&2; exit 1; }
x11_require_sys_dylibs

OBJ="$X11_ROOT/obj/spawn_test"
rm -rf "$OBJ"
mkdir -p "$OBJ"
x11_compile "$OBJ" spawn_test.c
x11_compile_report spawn_test

# libsystem_c first: it owns posix_spawnp; the rest (posix_spawn, kevent,
# pthread) come from the libSystem sys dylibs.
LINK=("$OUT/system/libsystem_c.dylib")
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o spawn_test.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OBJ"/*.o "${LINK[@]}" "${X11_SYS_DYLIBS[@]}"
echo "wrote spawn_test.macho"

bad=0
otool -l spawn_test.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: spawn_test: no LC_LOAD_DYLINKER /usr/lib/dyld" >&2; bad=1; }
otool -l spawn_test.macho | grep -q "cmd LC_LOAD_DYLIB" || {
    echo "error: spawn_test: no LC_LOAD_DYLIB" >&2; bad=1; }
otool -l spawn_test.macho | grep -q "cmd LC_MAIN" || {
    echo "error: spawn_test: no LC_MAIN" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1
./bind_target_audit.sh spawn_test.macho "${LINK[@]}"
