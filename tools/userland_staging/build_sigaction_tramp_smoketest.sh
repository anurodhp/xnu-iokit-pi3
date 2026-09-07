#!/bin/bash
# Builds the real signal-handler delivery test DAR-262 needs, TWICE:
#
#   libc_build/sigaction_tramp_smoketest.macho
#       linked against the real, fixed configd_sigaction_compat.c
#   libc_build/sigaction_tramp_smoketest_broken.macho
#       linked against sigaction_broken_shim_compat.c, a checked-in copy
#       of the pre-fix body
#
# Two binaries on purpose. A test that only ever passes proves the fix
# compiles, not that it fixes anything -- and this project has been
# bitten before by a "fix" whose failing case was never actually
# reproduced. Running both in one boot gives a real before/after on
# identical hardware, in the same run, with everything else held equal.
#
# See sigaction_tramp_smoketest.c's own header for what the three tests
# check and why "the handler ran" is not sufficient (the trampoline is
# on both sides of the handler; the sigreturn half is what a
# handler-ran-flag misses).
#
# Real link recipe copied from build_mach_port_construct_smoketest.sh
# (NEWLD_BINDIR override for Xcode 12's ld64-609 -fixup_chains crash,
# -DCRT_DYNAMIC_LINKING, generic_main_wrapper.c) -- read that script for
# the reasoning behind each, not repeated here.
#
# The one non-obvious ingredient: real Libc gen/FreeBSD/signal.c is
# compiled IN. signal(3) is not an export of any dylib in this stack
# (checked with dyld_info -exports, not assumed), and it is signal()'s
# real body that calls the `_sigaction` shim under test -- so pairing
# real signal.c with the shim is exactly the pairing configd.macho and
# notifyd.macho already have, which is the point.
set -euo pipefail
cd "$(dirname "$0")"
OUT="libc_build"
CSU="../../third_party/Csu"
LIBC="../../third_party/Libc"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi
if [ ! -d "$CSU" ] || [ ! -d "$LIBC" ]; then
    echo "error: $CSU / $LIBC not found -- run ./setup_third_party.sh first" >&2
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
# Same include set build_notifyd.sh uses for this exact file.
LIBC_CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -D_LIBC_NO_FEATURE_VERIFICATION -DPRIVATE=1 \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat \
    -Wno-implicit-function-declaration"
# -Wno-implicit-function-declaration: real signal.c calls `_sigaction`
# with no prototype in scope (real Apple builds it with its own internal
# namespace headers that declare it). That is the very symbol under test
# here, supplied by the shim object linked alongside. Same real source,
# same real call, prototype warning suppressed rather than the source
# edited.

"$CLANG" $CFLAGS -Dmain=sigtramp_real_main sigaction_tramp_smoketest.c \
    -o "$OUT/sigaction_tramp_smoketest.o"
"$CLANG" $CFLAGS -DREAL_MAIN=sigtramp_real_main -DCMD_NAME='"sigaction_tramp_smoketest"' \
    generic_main_wrapper.c -o "$OUT/sigaction_tramp_smoketest_wrapper.o"
"$CLANG" $LIBC_CFLAGS "$LIBC/gen/FreeBSD/signal.c" \
    -o "$OUT/sigaction_tramp_signal.o"

# The two shims under test.
"$CLANG" $CFLAGS configd_sigaction_compat.c \
    -o "$OUT/sigaction_tramp_shim_fixed.o"
"$CLANG" $CFLAGS sigaction_broken_shim_compat.c \
    -o "$OUT/sigaction_tramp_shim_broken.o"

link_one() {
    variant="$1"      # "" or "_broken"
    shim="$2"
    m="$OUT/sigaction_tramp_smoketest${variant}.macho"
    rm -f "$m"
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -DCRT_DYNAMIC_LINKING \
        -B"$NEWLD_BINDIR" \
        -Wl,-fixup_chains \
        -o "$m" \
        "$CSU/start.s" "$CSU/crt.c" \
        "$OUT/sigaction_tramp_smoketest_wrapper.o" \
        "$OUT/sigaction_tramp_smoketest.o" \
        "$OUT/sigaction_tramp_signal.o" \
        "$shim" \
        "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_pthread.dylib" \
        "$OUT/system/libsystem_malloc.dylib" "$OUT/system/libsystem_kernel.dylib" \
        "$OUT/system/libsystem_platform.dylib" \
        "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib"

    otool -l "$m" | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
        echo "error: $m has no LC_LOAD_DYLINKER /usr/lib/dyld" >&2
        exit 1
    }
    otool -l "$m" | grep -q "cmd LC_MAIN" || {
        echo "error: $m has no LC_MAIN" >&2
        exit 1
    }
    ./bind_target_audit.sh "$m" >/dev/null || {
        echo "error: $m has an unresolvable two-level bind" >&2
        exit 1
    }
    echo "wrote $m ($(wc -c < "$m") bytes)"
}

link_one ""       "$OUT/sigaction_tramp_shim_fixed.o"
link_one "_broken" "$OUT/sigaction_tramp_shim_broken.o"

# Prove the two binaries really differ in the way this test is about,
# rather than trusting that the right object got linked: the fixed one's
# _sigaction must call the public sigaction(3), the broken one must call
# the raw ___sigaction trap. Checked by disassembly because both objects
# define the same symbol with the same signature, so nm cannot tell them
# apart.
check_target() {
    m="$1"; want="$2"; label="$3"
    got="$(otool -tV -p __sigaction "$m" 2>/dev/null | sed -n '3,16p' \
           | grep -oE '_+sigaction$' | tail -1 || true)"
    if [ "$got" != "$want" ]; then
        echo "error: $m's _sigaction calls '${got:-<nothing>}', expected '$want' ($label)" >&2
        exit 1
    fi
    echo "verified: $(basename "$m") _sigaction -> $want ($label)"
}
check_target "$OUT/sigaction_tramp_smoketest.macho"        "_sigaction"  "real sigaction(3), via libplatform's __platform_sigaction"
check_target "$OUT/sigaction_tramp_smoketest_broken.macho" "___sigaction" "raw trap, DAR-262's pre-fix path"
