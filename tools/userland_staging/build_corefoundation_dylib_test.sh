#!/bin/bash
# Builds cf_dylib_smoketest.macho: real QEMU-bootable proof for DAR-195
# that libCoreFoundation.dylib's constructor/init-order actually works
# at real dyld load time, not just "links clean" -- see
# cf_dylib_smoketest.c's own header for exactly what it checks
# (__CFInitialized read cold, then real CFArray/CFString/CFUUID calls).
#
# A genuinely minimal, purely-dynamic consumer -- no static .a archives
# at all, same shape as build_dyld_test_exec.sh's own libtest.dylib
# smoketest, just against this real, much bigger dylib instead.
set -e
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
if [ ! -f "$OUT/system/libCoreFoundation.dylib" ]; then
    echo "error: $OUT/system/libCoreFoundation.dylib not found -- run build_corefoundation_dylib.sh first" >&2
    exit 1
fi

DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
    exit 1
fi

XNU="../../third_party/xnu-7195"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding \
    -fno-builtin -fno-stack-protector -O0 -Wall \
    -I "$OUT/cf_headers" -idirafter "$XNU/libkern" \
    cf_dylib_smoketest.c -o "$OUT/cf_dylib_smoketest_main.o"

# -DCRT_DYNAMIC_LINKING (DAR-161 pattern, see build_dyld_test_exec.sh's
# own header for the full real reasoning): this is a genuinely
# dynamically-linked executable, so Csu's own NXArgc/NXArgv/environ/
# __progname must resolve to libdyld.dylib's real copies, not a local
# static shadow.
rm -f "$OUT/cf_dylib_smoketest.macho"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o "$OUT/cf_dylib_smoketest.macho" \
    "$CSU/start.s" "$CSU/crt.c" "$OUT/cf_dylib_smoketest_main.o" \
    "$OUT/system/libCoreFoundation.dylib" \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib" \
    2>"$OUT/cf_dylib_smoketest_link.err" \
    && echo "wrote $OUT/cf_dylib_smoketest.macho" \
    || { echo "error: real link failed -- see $OUT/cf_dylib_smoketest_link.err" >&2; tail -60 "$OUT/cf_dylib_smoketest_link.err" >&2; exit 1; }

otool -l "$OUT/cf_dylib_smoketest.macho" | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: no LC_LOAD_DYLINKER /usr/lib/dyld -- not a real dynamic executable" >&2
    exit 1
}
otool -L "$OUT/cf_dylib_smoketest.macho" | grep -q "libCoreFoundation.dylib" || {
    echo "error: no LC_LOAD_DYLIB on libCoreFoundation.dylib" >&2
    exit 1
}
echo "confirmed: cf_dylib_smoketest.macho is a real dynamic executable linked against libCoreFoundation.dylib"
