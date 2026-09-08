#!/bin/bash
# Builds libc_build/tsd_vm_smoketest.macho -- the real, standalone
# fresh-thread TSD hygiene + anonymous-memory zero-fill test DAR-258
# needs. See tsd_vm_smoketest.c's own header comment for exactly which
# two candidate causes it discriminates between and why the question is
# expressible in plain POSIX terms with no libdispatch involved.
#
# Same real dynamic link recipe as build_kqueue_socket_smoketest.sh
# (NEWLD_BINDIR override, -Wl,-fixup_chains, -DCRT_DYNAMIC_LINKING,
# generic_main_wrapper.c) -- read that script for the full reasoning
# behind each of those, not repeated here. Dynamic on purpose: the
# threads DAR-258 is about are created by the same real
# libsystem_pthread.dylib every daemon binds to at runtime, so a static
# link would not be exercising the same provider.
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

CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 -Wall"

# Real command-entry convention (generic_main_wrapper.c): the real main()
# is renamed and wrapped so pthread_main_thread_bootstrap() runs before
# any TSD-backed Libc call. Skipping it is a real, reproducible SIGSEGV
# on this port, not a style preference -- and doubly load-bearing here,
# since this test is specifically about TSD.
"$CLANG" $CFLAGS -Dmain=tsdvm_real_main tsd_vm_smoketest.c \
    -o "$OUT/tsd_vm_smoketest.o"
"$CLANG" $CFLAGS -DREAL_MAIN=tsdvm_real_main -DCMD_NAME='"tsd_vm_smoketest"' \
    generic_main_wrapper.c -o "$OUT/tsd_vm_smoketest_wrapper.o"

rm -f "$OUT/tsd_vm_smoketest.macho"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -DCRT_DYNAMIC_LINKING \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o "$OUT/tsd_vm_smoketest.macho" \
    "$CSU/start.s" "$CSU/crt.c" \
    "$OUT/tsd_vm_smoketest_wrapper.o" "$OUT/tsd_vm_smoketest.o" \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libsystem_malloc.dylib" "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_platform.dylib" \
    "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib"

M="$OUT/tsd_vm_smoketest.macho"
otool -l "$M" | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: no LC_LOAD_DYLINKER /usr/lib/dyld -- not a real dynamic executable" >&2
    exit 1
}
otool -l "$M" | grep -q "cmd LC_MAIN" || {
    echo "error: no LC_MAIN -- see build_simple_cmds.sh's LINK_DYLIBS comment" >&2
    exit 1
}
# Real two-level-bind gate (see CLAUDE.md / bind_target_audit.sh): a clean
# link and a clean `nm -m` do NOT prove every bind resolves at load.
# Hard-gated, not merely run-if-present (2026-09-08, code review): this
# was the one sibling smoketest script in this family that only ran the
# audit when present and never checked its exit status, so a real
# unresolvable bind here would print "wrote ... real dynamic
# executable" and exit 0 -- exactly the DAR-217-class silent-abort
# CLAUDE.md documents this gate as existing to catch, in the one script
# that wasn't actually enforcing it.
./bind_target_audit.sh "$M" || {
    echo "error: $M has an unresolvable two-level bind -- it would abort at dyld load" >&2
    exit 1
}
echo "wrote $M ($(wc -c < "$M") bytes, real dynamic executable)"
