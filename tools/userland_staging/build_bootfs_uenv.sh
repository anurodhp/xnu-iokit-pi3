#!/bin/bash
# Builds bootfs_uenv.macho: rewrites uEnv.txt (kernel extra_bootargs) on the
# FAT32 BOOT partition from a running Pi; see bootfs_uenv.c. Dynamic link only;
# recipe as build_wutil_timer_test.sh / build_x11_probe.sh.
set -e
cd "$(dirname "$0")"
. ./x11_common.sh
x11_require_sys_dylibs

OBJ="$X11_ROOT/obj/bootfs_uenv"
rm -rf "$OBJ"
mkdir -p "$OBJ"
x11_compile "$OBJ" bootfs_uenv.c
x11_compile_report bootfs_uenv

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o bootfs_uenv.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OBJ"/*.o "${X11_SYS_DYLIBS[@]}"
echo "wrote bootfs_uenv.macho"

bad=0
otool -l bootfs_uenv.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: bootfs_uenv: no LC_LOAD_DYLINKER /usr/lib/dyld" >&2; bad=1; }
otool -l bootfs_uenv.macho | grep -q "cmd LC_LOAD_DYLIB" || {
    echo "error: bootfs_uenv: no LC_LOAD_DYLIB" >&2; bad=1; }
otool -l bootfs_uenv.macho | grep -q "cmd LC_MAIN" || {
    echo "error: bootfs_uenv: no LC_MAIN" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1
./bind_target_audit.sh bootfs_uenv.macho "${X11_SYS_DYLIBS[@]}"
