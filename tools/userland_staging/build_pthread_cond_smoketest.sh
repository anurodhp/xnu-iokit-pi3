#!/bin/bash
# Builds libc_build/pthread_cond_smoketest.macho -- the real mutex +
# condition-variable handoff probe DAR-280 needed. See
# pthread_cond_smoketest.c's own header comment for the crash this
# reproduces and what the real fix (DAR-280, real_bsdthread_register()
# in third_party/xnu-7195/bsd/kern/bsd_pthread_workq.c) changed.
#
# Same real dynamic link recipe as build_tlv_smoketest.sh - read that
# script's own comments for the full reasoning behind each flag, not
# repeated here. Dynamic on purpose, same as every other smoketest in
# this directory: a static binary bypasses dyld and libpthread's real
# runtime registration path entirely, which is exactly the mechanism
# under test here.
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
         system/libsystem_platform.dylib \
         system/libdyld.dylib system/libSystem.B.dylib; do
    if [ ! -f "$OUT/$f" ]; then
        echo "error: $OUT/$f not found -- run the libSystem tier build scripts first" >&2
        exit 1
    fi
done

CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 -Wall"

"$CLANG" $CFLAGS -Dmain=pthread_cond_real_main pthread_cond_smoketest.c \
    -o "$OUT/pthread_cond_smoketest.o"
"$CLANG" $CFLAGS -DREAL_MAIN=pthread_cond_real_main -DCMD_NAME='"pthread_cond_smoketest"' \
    generic_main_wrapper.c -o "$OUT/pthread_cond_smoketest_wrapper.o"

rm -f "$OUT/pthread_cond_smoketest.macho"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o "$OUT/pthread_cond_smoketest.macho" \
    "$CSU/start.s" "$CSU/crt.c" \
    "$OUT/pthread_cond_smoketest_wrapper.o" \
    "$OUT/pthread_cond_smoketest.o" \
    "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_platform.dylib" \
    "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib"

M="$OUT/pthread_cond_smoketest.macho"
otool -l "$M" | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: no LC_LOAD_DYLINKER /usr/lib/dyld -- not a real dynamic executable" >&2
    exit 1
}
otool -l "$M" | grep -q "cmd LC_MAIN" || {
    echo "error: no LC_MAIN -- see build_simple_cmds.sh's LINK_DYLIBS comment" >&2
    exit 1
}
./bind_target_audit.sh "$M" || {
    echo "error: $M has an unresolvable two-level bind -- it would abort at dyld load" >&2
    exit 1
}
echo "wrote $M ($(wc -c < "$M") bytes, real dynamic executable)"
