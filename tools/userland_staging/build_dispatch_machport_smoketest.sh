#!/bin/bash
# Builds libc_build/dispatch_machport_smoketest.macho -- the real,
# standalone EVFILT_MACHPORT/dispatch-source probe DAR-266/DAR-268 need.
# See dispatch_machport_smoketest.c's own header comment for exactly
# which primitives each argv mode separates and why none of them has
# ever run on this port before.
#
# Same real dynamic link recipe as build_mach_port_construct_smoketest.sh
# / build_notifyd_smoketest.sh (NEWLD_BINDIR override,
# -Wl,-fixup_chains, -DCRT_DYNAMIC_LINKING, generic_main_wrapper.c) --
# read build_tsd_vm_smoketest.sh for the full reasoning behind each of
# those, not repeated here. Dynamic on purpose: the whole point is to
# exercise the SAME real libdispatch.dylib / libsystem_notify.dylib
# bodies configd itself binds to at runtime.
#
# Two differences from the other smoketest scripts, both real
# requirements of what is under test:
#   -fblocks + libsystem_blocks.dylib -- notify_client.c:1007 hands
#     dispatch_source_set_cancel_handler() a real Block, and the
#     `recv-block` mode reproduces that exact shape, so this test needs
#     real Blocks support rather than function pointers only.
#   libdispatch.dylib is listed FIRST after libsystem_kernel.dylib so
#     every dispatch_* bind records against its real owning dylib
#     (CLAUDE.md's DAR-217 two-level-bind rule; bind_target_audit.sh
#     below is the hard gate, not a formality).
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
         system/libsystem_platform.dylib system/libsystem_blocks.dylib \
         system/libdispatch.dylib system/libsystem_notify.dylib \
         system/libxpc.dylib \
         system/libdyld.dylib system/libSystem.B.dylib; do
    if [ ! -f "$OUT/$f" ]; then
        echo "error: $OUT/$f not found -- run the libSystem tier build scripts first" >&2
        exit 1
    fi
done

CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 -Wall -fblocks"

# Real command-entry convention (generic_main_wrapper.c): the real main()
# is renamed and wrapped so pthread_main_thread_bootstrap() runs before
# any TSD-backed Libc call. Skipping it is a real, reproducible SIGSEGV
# on this port, not a style preference.
"$CLANG" $CFLAGS -Dmain=dmp_real_main dispatch_machport_smoketest.c \
    -o "$OUT/dispatch_machport_smoketest.o"
"$CLANG" $CFLAGS -DREAL_MAIN=dmp_real_main -DCMD_NAME='"dispatch_machport_smoketest"' \
    generic_main_wrapper.c -o "$OUT/dispatch_machport_smoketest_wrapper.o"

rm -f "$OUT/dispatch_machport_smoketest.macho"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o "$OUT/dispatch_machport_smoketest.macho" \
    "$CSU/start.s" "$CSU/crt.c" \
    "$OUT/dispatch_machport_smoketest_wrapper.o" \
    "$OUT/dispatch_machport_smoketest.o" \
    "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libdispatch.dylib" \
    "$OUT/system/libsystem_notify.dylib" \
    "$OUT/system/libsystem_blocks.dylib" \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_platform.dylib" \
    "$OUT/system/libxpc.dylib" \
    "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib"

M="$OUT/dispatch_machport_smoketest.macho"
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
