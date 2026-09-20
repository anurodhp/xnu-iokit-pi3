#!/bin/bash
# Builds libc_build/fork_alarm_smoketest.macho -- the standalone
# reproducer for DAR-336 (alarm(3) kills the caller) and DAR-337 (a
# fork() child that forks again crashes), both found by the OpenSSH port
# (DAR-183). See fork_alarm_smoketest.c's own header for what each step
# proves and why a crash is located by which line is MISSING.
#
# Same real dynamic-link recipe as build_kqueue_socket_smoketest.sh (its
# own header explains each flag) -- deliberately dynamic, because the
# binaries that hit these bugs are dynamic and must bind to the same real
# providers of fork/sigaction/setitimer.
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
         system/libsystem_platform.dylib system/libdyld.dylib \
         system/libSystem.B.dylib; do
    if [ ! -f "$OUT/$f" ]; then
        echo "error: $OUT/$f not found -- run the libSystem tier build scripts first" >&2
        exit 1
    fi
done

CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 -Wall"

# generic_main_wrapper.c: real command-entry convention here -- the real
# main() is renamed and wrapped so pthread_main_thread_bootstrap() runs
# before any TSD-backed Libc call.
"$CLANG" $CFLAGS -Dmain=fas_real_main fork_alarm_smoketest.c \
    -o "$OUT/fork_alarm_smoketest.o"
"$CLANG" $CFLAGS -DREAL_MAIN=fas_real_main -DCMD_NAME='"fork_alarm_smoketest"' \
    generic_main_wrapper.c -o "$OUT/fork_alarm_smoketest_wrapper.o"

rm -f "$OUT/fork_alarm_smoketest.macho"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o "$OUT/fork_alarm_smoketest.macho" \
    "$CSU/start.s" "$CSU/crt.c" \
    "$OUT/fork_alarm_smoketest_wrapper.o" "$OUT/fork_alarm_smoketest.o" \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libsystem_malloc.dylib" "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_platform.dylib" \
    "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib"

M="$OUT/fork_alarm_smoketest.macho"
otool -l "$M" | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: no LC_LOAD_DYLINKER /usr/lib/dyld -- not a real dynamic executable" >&2
    exit 1
}
otool -l "$M" | grep -q "cmd LC_MAIN" || {
    echo "error: no LC_MAIN" >&2; exit 1
}
./bind_target_audit.sh "$M" || {
    echo "error: $M failed bind_target_audit.sh" >&2; exit 1
}
echo "wrote $M ($(wc -c < "$M") bytes, real dynamic executable)"
