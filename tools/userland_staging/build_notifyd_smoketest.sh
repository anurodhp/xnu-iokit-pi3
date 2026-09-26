#!/bin/bash
# Builds libc_build/notifyd_roundtrip_smoketest.macho -- DAR-167's real
# end-to-end verification client. See notifyd_roundtrip_smoketest.c's
# own header comment for exactly what it proves. Same real dynamic-link
# recipe as build_kqueue_socket_smoketest.sh (generic_main_wrapper.c +
# Csu start.s/crt.c + NEWLD_BINDIR override for Xcode 12's ld64-609
# -fixup_chains crash, docs/backlog.md item d9) -- not repeated here.
#
# Deliberately links libSystem.B.dylib/libsystem_notify.dylib (unlike
# notifyd.macho itself): this is a fresh, separate client process, not
# the daemon -- see build_notifyd.sh's own header comment for why THAT
# binary can't link these same dylibs.
set -euo pipefail
cd "$(dirname "$0")"
OUT="libc_build"
CSU="../../third_party/Csu"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi
if [ ! -d "$CSU" ]; then
    echo "error: $CSU not found -- run ./setup_third_party.sh first" >&2
    exit 1
fi

DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE)" >&2
    exit 1
fi

for f in system/libsystem_c.dylib system/libsystem_pthread.dylib \
         system/libsystem_malloc.dylib system/libsystem_kernel.dylib \
         system/libsystem_platform.dylib system/libdyld.dylib system/libSystem.B.dylib \
         system/libsystem_notify.dylib; do
    if [ ! -f "$OUT/$f" ]; then
        echo "error: $OUT/$f not found -- run the libSystem tier build scripts first" >&2
        exit 1
    fi
done

CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 -Wall"

"$CLANG" $CFLAGS -Dmain=notifyd_rt_real_main notifyd_roundtrip_smoketest.c \
    -o "$OUT/notifyd_roundtrip_smoketest.o"
"$CLANG" $CFLAGS -DREAL_MAIN=notifyd_rt_real_main -DCMD_NAME='"notifyd_roundtrip_smoketest"' \
    generic_main_wrapper.c -o "$OUT/notifyd_roundtrip_smoketest_wrapper.o"

rm -f "$OUT/notifyd_roundtrip_smoketest.macho"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o "$OUT/notifyd_roundtrip_smoketest.macho" \
    "$CSU/start.s" "$CSU/crt.c" \
    "$OUT/notifyd_roundtrip_smoketest_wrapper.o" "$OUT/notifyd_roundtrip_smoketest.o" \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libsystem_malloc.dylib" "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_platform.dylib" \
    "$OUT/system/libsystem_notify.dylib" \
    "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib"

M="$OUT/notifyd_roundtrip_smoketest.macho"
otool -l "$M" | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: no LC_LOAD_DYLINKER /usr/lib/dyld -- not a real dynamic executable" >&2
    exit 1
}
otool -l "$M" | grep -q "cmd LC_LOAD_DYLIB" || {
    echo "error: no LC_LOAD_DYLIB -- links no dylib at all, not a real dynamic executable" >&2
    exit 1
}
otool -l "$M" | grep -q "cmd LC_MAIN" || {
    echo "error: no LC_MAIN -- see build_simple_cmds.sh's LINK_DYLIBS comment" >&2
    exit 1
}
echo "wrote $M ($(wc -c < "$M") bytes, real dynamic executable)"

./bind_target_audit.sh "$M" || {
    echo "error: $M has an unresolvable two-level bind -- it would abort at dyld load" >&2
    exit 1
}
