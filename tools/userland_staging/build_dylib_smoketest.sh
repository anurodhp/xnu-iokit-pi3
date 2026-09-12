#!/bin/bash
# Phase 6.5 Phase A (docs/daemon-roadmap.md): builds libtest.dylib, a real
# .dylib produced by this project's pinned Xcode-12 ld64 -- the first dylib
# this toolchain has ever emitted. Every other build_*.sh here targets
# -static output on purpose (see CLAUDE.md); this one is the first
# deliberate step off that path, additive only -- it does not touch any
# existing static script.
#
# Purely a load-command-shape proof (MH_DYLIB / LC_ID_DYLIB); no real dyld
# exists yet to ever load this file, so it is not installed onto any SD
# image. build_dyld_test_exec.sh links against it next.
set -e
cd "$(dirname "$0")"

OUT="libc_build"
mkdir -p "$OUT"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi

# Real prerequisite, added 2026-09-12 (DAR-301): this script links
# against "$OUT/system/libSystem.B.dylib" directly (see the real
# LC_LOAD_DYLIB requirement explained below) but never declared that as
# a checked dependency in either idiom gen_build_order.py recognizes --
# it just let clang fail with a raw "no such file" error. Found because
# a real, live build_everything.sh run scheduled this script at position
# 5, 90 slots before build_libsystem.sh (the real builder) at 95.
if [ ! -f "$OUT/system/libSystem.B.dylib" ]; then
    echo "error: $OUT/system/libSystem.B.dylib not found -- run build_libsystem.sh first" >&2
    exit 1
fi

# Real, verified fix (2026-08-28, see docs/backlog.md item d9 for the
# full trail and tools/userland_staging/build_simple_cmds.sh's own
# NEWLD_BINDIR header comment for the isolated repro this is based on):
# Xcode 12's own bundled ld64-609 has a real internal crash under
# -Wl,-fixup_chains that a much newer ld does not. Compiling still uses
# Xcode 12's clang/SDK above (untouched -- that vintage match is a
# separate, still-load-bearing invariant); only the final link step's
# linker binary changes, via clang's -B override.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
    exit 1
fi

# -Wl,-fixup_chains: real dyld-940 (this project's currently pinned
# tag) expects the modern chained-fixups format -- see
# build_dyld_test_exec.sh's own header comment for the full real
# crash this fixed (EXC_BAD_ACCESS, fault address 0x10, in real dyld's
# JustInTimeLoader bind/fixup code against the older LC_DYLD_INFO_ONLY
# bind-opcode format ld64 emits by default).
#
# -nostdlib: real bug found+fixed 2026-08-25 -- without this, ld64's
# default (non-static) dylib link auto-adds a real LC_LOAD_DYLIB on
# /usr/lib/libSystem.B.dylib (confirmed via otool -l), which this
# project doesn't have yet (Phase 6.5 Phase D territory, a much larger,
# separate port -- see CLAUDE.md's own dyld-vs-libSystem architecture
# note). libtest_dylib.c calls zero real Libc functions, so it never
# needed that dependency in the first place; -nostdlib drops it so real
# dyld's own dependency-loading path has nothing left to resolve,
# letting the Phase 6.5 Phase C kernel-handoff/dyld-bootstrap test
# actually run dyld_test_exec's real main() to completion instead of
# stopping at "Library not loaded: /usr/lib/libSystem.B.dylib".
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/libtest.dylib \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o "$OUT/libtest.dylib" libtest_dylib.c "$OUT/system/libSystem.B.dylib"
# "$OUT/system/libSystem.B.dylib" added 2026-08-28 (docs/backlog.md item
# d9's NEWLD_BINDIR sweep): real, newly-enforced requirement of the
# daily-driver Xcode's newer ld ("dynamic executables or dylibs must
# link with libSystem.dylib") that Xcode 12's ld64-609 never checked --
# this project's own real libSystem.B.dylib satisfies it; libtest_dylib.c
# itself still references nothing from it, so this is a harmless, real
# LC_LOAD_DYLIB dependency, not a functional change to the smoketest.
echo "wrote $OUT/libtest.dylib"

otool -hv "$OUT/libtest.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB -- ld64 did not produce a real dylib" >&2
    exit 1
}
otool -l "$OUT/libtest.dylib" | grep -q LC_ID_DYLIB || {
    echo "error: no LC_ID_DYLIB -- not a real dylib" >&2
    exit 1
}
echo "confirmed: $OUT/libtest.dylib is a real MH_DYLIB with LC_ID_DYLIB"
