#!/bin/bash
# Builds tools/userland_staging/gnustep_objc4_smoketest.macho (DAR-382).
#
# DAR-382 chose the Objective-C runtime for GNUstep on this port: Apple's
# objc4 (libobjc.A.dylib, build_objc4.sh), with gnustep-base built as the
# Foundation layer on top of it (gnustep-make library combo apple-gnu-gnu).
# The reasons, each checked on 2026-09-26:
#
#   - libobjc2 cannot target Mach-O with any ABI. gnustep-2.0 is refused
#     outright ("GNUstep Objective-C runtime version 2 incompatible with
#     target binary format"), and gnustep-1.x and gcc crash the backend
#     ("MachO doesn't support COMDATs, '.objc_sel_namealloc' cannot be
#     lowered") with both Xcode 12's clang and the daily-driver clang:
#     CGObjCGNU emits every selector name as a linkonce_odr COMDAT
#     (clang/lib/CodeGen/CGObjCGNU.cpp:216-231 at llvmorg-11.1.0,
#     ExportUniqueString). Using it would mean forking and building clang.
#   - objc4 is the runtime Apple ships, is already built and QEMU-verified
#     (DAR-236), and one runtime per system is the only safe shape.
#
# What the objc4 route costs gnustep-base (DAR-388's work, not this
# script's), from a -fsyntax-only probe of all 223 libs-base Source/*.m
# files against objc4's headers:
#   - libobjc owns NSObject (objc4 runtime/NSObject.mm). gnustep-base's
#     NSObject.m @implementation must become categories on it, as Apple's
#     own Foundation/CF does (NSObject.mm:2452-2460, "Replaced by CF").
#   - ~200 uncast IMP calls. objc4 forces typed dispatch prototypes on arm64
#     (runtime/objc-api.h:121-126), and an uncast variadic IMP call is also
#     ABI-wrong on Apple arm64, where variadic arguments go on the stack.
#   - <objc/encoding.h> (objc_sizeof_type and friends) is a libobjc2 header
#     with no objc4 counterpart.
#
# This smoketest proves the runtime features that plan relies on: a second
# root class, categories on libobjc's NSObject and on a custom root,
# +load/+initialize order, a typed IMP call, @try/@catch/@finally, blocks,
# associated objects, objc_setForwardHandler, weak references and
# autorelease pools. Link line is build_objc4.sh's own smoketest link.
set -e
cd "$(dirname "$0")"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
    exit 1
fi
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE)" >&2
    exit 1
fi

LIBC="../../third_party/Libc"
CLOSURE="../../third_party/libclosure"
OUT="libc_build"
PUBHDRS="$OUT/objc4_public_headers"

if [ ! -f "$OUT/libobjc.A.dylib" ] || [ ! -d "$PUBHDRS/objc" ]; then
    echo "error: $OUT/libobjc.A.dylib not built -- run build_objc4.sh first" >&2
    exit 1
fi
for dylib in libsystem_kernel libsystem_platform libsystem_blocks libsystem_pthread \
             libsystem_malloc libsystem_c libsystem_notify libsystem_m libc++ libunwind libdyld libSystem.B; do
    if [ ! -f "$OUT/system/$dylib.dylib" ]; then
        echo "error: $OUT/system/$dylib.dylib not found -- run its build_*_dylib.sh first" >&2
        exit 1
    fi
done

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -fno-objc-arc -fobjc-exceptions -fblocks \
    -I "$PUBHDRS" -I "$CLOSURE" -I "$LIBC/include" -I "$LIBC/darwin" -I "$OUT/libc_features_gen/arm64" \
    -Wno-nullability-completeness -Wno-objc-root-class \
    gnustep_objc4_smoketest.m -o "$OUT/gnustep_objc4_smoketest.o"

rm -f gnustep_objc4_smoketest.macho
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o gnustep_objc4_smoketest.macho \
    -DCRT_NO_PTHREAD_BOOTSTRAP ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT/gnustep_objc4_smoketest.o" \
    "$OUT/libobjc.A.dylib" \
    "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_platform.dylib" \
    "$OUT/system/libsystem_blocks.dylib" \
    "$OUT/system/libsystem_pthread.dylib" \
    "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_c.dylib" \
    "$OUT/system/libsystem_notify.dylib" \
    "$OUT/system/libsystem_m.dylib" \
    "$OUT/system/libc++.dylib" \
    "$OUT/system/libunwind.dylib" \
    "$OUT/system/libdyld.dylib" \
    "$OUT/system/libSystem.B.dylib" \
    2>"$OUT/gnustep_objc4_smoketest_link.err" \
    && echo "real link: 0 undefined symbols -- gnustep_objc4_smoketest.macho fully closed" \
    || { grep '^  "' "$OUT/gnustep_objc4_smoketest_link.err" | sed 's/,.*//' | sort -u; tail -40 "$OUT/gnustep_objc4_smoketest_link.err" >&2; exit 1; }

./bind_target_audit.sh gnustep_objc4_smoketest.macho || {
    echo "error: gnustep_objc4_smoketest.macho has an unresolvable two-level bind" >&2
    exit 1
}
# gap_audit.sh takes paths relative to the repo root.
GAP_OUT=$(./gap_audit.sh tools/userland_staging/gnustep_objc4_smoketest.macho \
    tools/userland_staging/$OUT/libobjc.A.dylib tools/userland_staging/$OUT/system/libSystem.B.dylib 2>&1) || {
    echo "$GAP_OUT" >&2
    echo "error: gap_audit.sh failed on gnustep_objc4_smoketest.macho" >&2
    exit 1
}
echo "$GAP_OUT" | tail -3

echo "wrote gnustep_objc4_smoketest.macho"
