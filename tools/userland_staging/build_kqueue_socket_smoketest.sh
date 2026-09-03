#!/bin/bash
# Builds libc_build/kqueue_socket_smoketest.macho -- the real, standalone
# EVFILT_READ / EVFILT_PROC / select()-on-a-kqueue-fd test DAR-200 calls
# for. See kqueue_socket_smoketest.c's own header comment for exactly
# which four things it proves and why its older sibling
# kqueue_smoketest.c (EVFILT_TIMER only, never calls select()) cannot
# answer the same question.
#
# Deliberately a REAL DYNAMIC link (LC_LOAD_DYLINKER /usr/lib/dyld +
# LC_MAIN), not the fully-static shape build_kqueue_smoketest.sh still
# uses: launchd.macho is itself dynamically linked, so a static test
# would not be exercising the same real select()/kevent() providers
# launchd actually binds to at runtime. Same link recipe as
# build_simple_cmds.sh's own 70+ real commands (NEWLD_BINDIR override,
# -Wl,-fixup_chains, -DCRT_DYNAMIC_LINKING, generic_main_wrapper.c) --
# see that script and build_dyld_test_exec.sh for the full reasoning
# behind each of those, not repeated here.
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

# See build_simple_cmds.sh's own NEWLD_BINDIR comment / docs/backlog.md
# item d9: Xcode 12's bundled ld64-609 crashes under -Wl,-fixup_chains.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE)" >&2
    exit 1
fi

for f in system/libsystem_c.dylib system/libsystem_pthread.dylib \
         system/libsystem_malloc.dylib system/libsystem_kernel.dylib \
         system/libdyld.dylib system/libSystem.B.dylib; do
    if [ ! -f "$OUT/$f" ]; then
        echo "error: $OUT/$f not found -- run the libSystem tier build scripts first" >&2
        exit 1
    fi
done

CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 -Wall"

# Real command-entry convention (generic_main_wrapper.c): the real main()
# is renamed and wrapped so pthread_main_thread_bootstrap() runs before
# any TSD-backed Libc call. Skipping it is a real, reproducible SIGSEGV
# on this port, not a style preference.
"$CLANG" $CFLAGS -Dmain=kqs_real_main kqueue_socket_smoketest.c \
    -o "$OUT/kqueue_socket_smoketest.o"
"$CLANG" $CFLAGS -DREAL_MAIN=kqs_real_main -DCMD_NAME='"kqueue_socket_smoketest"' \
    generic_main_wrapper.c -o "$OUT/kqueue_socket_smoketest_wrapper.o"

rm -f "$OUT/kqueue_socket_smoketest.macho"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o "$OUT/kqueue_socket_smoketest.macho" \
    "$CSU/start.s" "$CSU/crt.c" \
    "$OUT/kqueue_socket_smoketest_wrapper.o" "$OUT/kqueue_socket_smoketest.o" \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libsystem_malloc.dylib" "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib"

M="$OUT/kqueue_socket_smoketest.macho"
otool -l "$M" | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: no LC_LOAD_DYLINKER /usr/lib/dyld -- not a real dynamic executable" >&2
    exit 1
}
otool -l "$M" | grep -q "cmd LC_MAIN" || {
    echo "error: no LC_MAIN -- see build_simple_cmds.sh's LINK_DYLIBS comment" >&2
    exit 1
}
echo "wrote $M ($(wc -c < "$M") bytes, real dynamic executable)"
