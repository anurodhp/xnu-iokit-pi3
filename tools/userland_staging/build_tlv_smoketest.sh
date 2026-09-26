#!/bin/bash
# Builds libc_build/tlv_smoketest.macho -- the real arm64 __thread /
# Thread-Local Variable probe DAR-279 needed. See tlv_smoketest.c's own
# header comment for what each check distinguishes and why a
# single-threaded "does it not crash" test would not have been enough.
#
# Same real dynamic link recipe as build_dispatch_machport_smoketest.sh
# (NEWLD_BINDIR override, -Wl,-fixup_chains, -DCRT_DYNAMIC_LINKING,
# generic_main_wrapper.c) -- read build_tsd_vm_smoketest.sh for the full
# reasoning behind each of those, not repeated here. Dynamic on purpose:
# TLV support IS a dyld feature. A static binary has no TLV descriptors
# for dyld to fix up and would prove nothing about the thing under test.
#
# The one thing this script must not do is link -static or otherwise
# bypass dyld, and the LC_LOAD_DYLINKER assert at the bottom is what
# keeps that true.
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
         system/libsystem_platform.dylib \
         system/libdyld.dylib system/libSystem.B.dylib; do
    if [ ! -f "$OUT/$f" ]; then
        echo "error: $OUT/$f not found -- run the libSystem tier build scripts first" >&2
        exit 1
    fi
done

# Hard prerequisite check, not a courtesy: this test is meaningless
# against a libdyld.dylib whose _tlv_get_addr is still the old
# `return 0;` stand-in -- it would just crash and read as "TLV is
# broken" no matter what else changed. Assert the real arm64 fast path
# is present in the dylib this binary will actually load.
if ! otool -tV "$OUT/system/libdyld.dylib" | grep -q "TPIDRRO_EL0"; then
    echo "error: $OUT/system/libdyld.dylib has no TPIDRRO_EL0 read -- its" >&2
    echo "       _tlv_get_addr is not the real arm64 fast path. Run" >&2
    echo "       build_libdyld.sh (DAR-279) before this script." >&2
    exit 1
fi

CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 -Wall"

# Real command-entry convention (generic_main_wrapper.c): the real main()
# is renamed and wrapped so pthread_main_thread_bootstrap() runs before
# any TSD-backed Libc call. Skipping it is a real, reproducible SIGSEGV
# on this port, not a style preference -- and it matters doubly here,
# since _tlv_get_addr reads the thread's TSD array directly off
# TPIDRRO_EL0 and needs that bootstrap to have happened.
"$CLANG" $CFLAGS -Dmain=tlv_real_main tlv_smoketest.c \
    -o "$OUT/tlv_smoketest.o"
"$CLANG" $CFLAGS -DREAL_MAIN=tlv_real_main -DCMD_NAME='"tlv_smoketest"' \
    generic_main_wrapper.c -o "$OUT/tlv_smoketest_wrapper.o"

# Prove the compiler actually emitted TLV descriptors rather than
# silently demoting the __thread variables to plain globals -- which
# would make every check below pass while testing nothing at all.
otool -l "$OUT/tlv_smoketest.o" | grep -q "__thread_vars" || {
    echo "error: $OUT/tlv_smoketest.o has no __DATA,__thread_vars section --" >&2
    echo "       the __thread variables did not become real TLVs" >&2
    exit 1
}

rm -f "$OUT/tlv_smoketest.macho"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o "$OUT/tlv_smoketest.macho" \
    "$CSU/start.s" "$CSU/crt.c" \
    "$OUT/tlv_smoketest_wrapper.o" \
    "$OUT/tlv_smoketest.o" \
    "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_platform.dylib" \
    "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib"

M="$OUT/tlv_smoketest.macho"
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
otool -l "$M" | grep -q "__thread_vars" || {
    echo "error: no __DATA,__thread_vars in the linked binary -- nothing for" >&2
    echo "       dyld to set up, so this test would prove nothing" >&2
    exit 1
}
nm -u "$M" | grep -q "^__tlv_bootstrap$" || {
    echo "error: $M does not import __tlv_bootstrap -- its TLV descriptors" >&2
    echo "       were not left for dyld to fix up" >&2
    exit 1
}
./bind_target_audit.sh "$M" || {
    echo "error: $M has an unresolvable two-level bind -- it would abort at dyld load" >&2
    exit 1
}
echo "wrote $M ($(wc -c < "$M") bytes, real dynamic executable)"
