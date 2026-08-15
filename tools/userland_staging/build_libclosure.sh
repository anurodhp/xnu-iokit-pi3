#!/bin/bash
# Builds libc_build/libclosure.a: real Apple libclosure (the Blocks
# runtime - Block_copy/Block_release/Block_object_assign/
# Block_object_dispose, stack->heap block promotion, refcounting).
# third_party/libclosure is already cloned+pinned (libclosure-78, see
# setup_third_party.sh; re-verified here against
# apple-oss-distributions/distribution-macOS tag macos-112's
# `git ls-tree macos-112 -- libclosure` = commit 86c3a9ea6bef05f...,
# matching this checkout's HEAD exactly - not just trusting the existing
# pin blindly). Continues the tier structure of build_libc_string.sh/
# build_libplatform.sh/build_libc_locale.sh/build_libc_stdio.sh/
# build_libc_gen.sh/build_libc_printf.sh/build_libmalloc.sh/
# build_libpthread.sh (see docs/roadmap.md Phase 4 for the running tier
# breakdown).
#
# Scope: real runtime.cpp (the actual Block_copy/Block_release/
# Block_object_assign/Block_object_dispose logic - refcounting via
# BLOCK_REFCOUNT_MASK, latching_incr_int/latching_decr_int using
# __sync_bool_compare_and_swap, a real compiler builtin lowering to a
# real arm64 atomic instruction sequence with zero runtime-library
# dependency - confirmed by disassembling the resulting .o and seeing
# real ldxr/stxr, not a libcall) + real data.c (the plain
# _NSConcreteStackBlock/_NSConcreteMallocBlock/_NSConcreteAutoBlock/
# _NSConcreteFinalizingBlock/_NSConcreteGlobalBlock/
# _NSConcreteWeakBlockVariable symbol table, #if !HAVE_OBJC-gated - this
# project has no ObjC runtime, so that's the correct, real branch, not a
# skip). This target has no ObjC runtime and no dyld/Foundation to ever
# call _Block_use_RR2 - runtime.cpp's own real default retain/release/
# destructInstance callbacks (_Block_retain_object_default etc, all
# real no-ops until/unless something installs ObjC-aware ones) are
# exactly correct for that, not a gap.
#
# Genuinely out of scope, not attempted: data.m (ObjC-only counterpart
# of data.c, mutually exclusive with it - this project builds the
# !HAVE_OBJC side only) and libclosure-vs2005/escapeTests/Examples/
# objectTests/RewriterBlockTest (Windows port + standalone test-harness
# subprojects, no bearing on the real runtime).
#
# One real cross-tier dependency found while bringing this up: runtime.cpp
# calls plain memmove() via `#define memmove _platform_memmove` (real
# Apple source, not a workaround), and _platform_memmove's real generic
# (non-arch-optimized) implementation lives in
# third_party/libplatform/src/string/generic/memmove.c, gated
# `#if !_PLATFORM_OPTIMIZED_MEMMOVE` (real value 0 for this SDK vintage
# per platform/string.h - confirmed by reading that file, not guessed).
# Compiled here WITHOUT -DVARIANT_STATIC: that flag makes the same file
# also emit plain `memmove`/`memcpy` symbols, which would collide with
# libc_string.a's own real memcpy/memmove (a genuine duplicate-symbol
# link error, caught by trying it before writing this comment) - this
# tier only needs the `_platform_memmove` entry point, so VARIANT_STATIC
# is deliberately left off for this one file.
#
# os_assumes/os_assert: runtime.cpp's own header guards
# (`#ifndef os_assumes #define os_assumes(_x) os_assumes(_x)`) mean that
# if os_assumes is NOT already a defined macro when runtime.cpp is
# compiled, it falls back to calling a real external function of that
# name - which this project has never defined (the existing
# libc_build/compat_include/os/assumes.h from build_libc_locale.sh is a
# deliberately empty stand-in, unused by anything until now - see that
# file's own comment). Rather than add a second, conflicting os/assumes.h
# forwarding path, this script predefines os_assumes/os_assert as
# command-line function-like macros (-D, below) BEFORE runtime.cpp's own
# #ifndef check runs, matching Apple's own real fallback shape for a
# build with no os_log/os_trace subsystem: os_assumes(x) simply evaluates
# to (x) (used in `switch(os_assumes(flags & MASK))` - needs the real
# value, not a bool), and os_assert(x) is a real assert - traps via a
# genuine compiler builtin (__builtin_trap(), a real illegal-instruction
# halt, not a silent no-op) if the invariant it's checking is false,
# rather than silently discarding it.
set -e
cd "$(dirname "$0")"

CLOSURE="../../third_party/libclosure"
PLATFORM="../../third_party/libplatform"
OUT="libc_build"
mkdir -p "$OUT/libclosure"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
if [ ! -f "$OUT/libmalloc.a" ]; then
    echo "error: $OUT/libmalloc.a not found -- run build_libmalloc.sh first (real malloc/free, this tier's real allocator)" >&2
    exit 1
fi

CFLAGS_COMMON="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -I $CLOSURE -I $PLATFORM/private -I $OUT/compat_include"

ok=0
skip=0

# data.c: plain C, the real !HAVE_OBJC NSConcrete*Block symbol table.
"$CLANG" $CFLAGS_COMMON "$CLOSURE/data.c" -o "$OUT/libclosure/data.o"
ok=$((ok + 1))

# _platform_memmove: real generic (portable) implementation, see header
# comment above for why VARIANT_STATIC is deliberately not set here.
"$CLANG" $CFLAGS_COMMON "$PLATFORM/src/string/generic/memmove.c" -o "$OUT/libclosure/memmove.o"
ok=$((ok + 1))

# runtime.cpp: real C++ Blocks runtime. -fno-exceptions/-fno-rtti: this
# target has no real libunwind/libc++abi yet (separate, not-yet-attempted
# Phase 4 item - see docs/roadmap.md), and runtime.cpp itself throws
# nothing and needs no RTTI - confirmed by grep (zero `throw`/`catch`/
# `dynamic_cast`/`typeid` in the file) before relying on this, not
# assumed safe.
"$CLANG" $CFLAGS_COMMON -x c++ -std=gnu++14 -fno-exceptions -fno-rtti \
    '-Dos_assumes(x)=(x)' \
    '-Dos_assert(x)=((x)?(void)0:__builtin_trap())' \
    "$CLOSURE/runtime.cpp" -o "$OUT/libclosure/runtime.o"
ok=$((ok + 1))

echo "libclosure: $ok compiled, $skip skipped"

rm -f "$OUT/libclosure.a"
ar rcs "$OUT/libclosure.a" "$OUT"/libclosure/*.o
echo "wrote $OUT/libclosure.a"

# Real smoketest: a real compiler-emitted stack block (-fblocks) capturing
# a local variable, promoted to the heap via a real Block_copy, called,
# verified, released via a real Block_release. A second block below adds
# a __block-qualified byref variable (BLOCK_HAS_COPY_DISPOSE path,
# exercising _Block_object_assign/_Block_object_dispose for real, not
# just the fast/no-copy-helper path the first block takes).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -fblocks -I "$CLOSURE" \
    libclosure_smoketest.c -o "$OUT/libclosure_smoketest.o"
# Same real dependency set + $UNIX2003 aliases as
# build_libmalloc.sh's own smoketest link line (libclosure.a's real
# malloc/free calls need exactly what malloc's own smoketest needs -
# mirrored here rather than re-derived, since it's the same real
# requirement, not a coincidence).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
    -o libclosure_smoketest.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OUT/libclosure_smoketest.o" "$OUT/libclosure.a" \
    "$OUT/libmalloc.a" "$OUT/libplatform.a" "$OUT/cerror_stub.o" \
    "$OUT/libc_locale_for_stdio.a" "$OUT/libc_stdio.a" \
    -Wl,-alias,_mprotect,'_mprotect$UNIX2003' \
    -Wl,-alias,___kill,'_kill$UNIX2003' \
    -Wl,-alias,___kill,_kill \
    -Wl,-alias,_write,'_write$UNIX2003' \
    -Wl,-alias,__mal_semwait_sleep,'_sleep$UNIX2003' \
    -Wl,-alias,___kdebug_trace,_kdebug_trace
echo "wrote libclosure_smoketest.macho"

otool -l libclosure_smoketest.macho | grep -q LC_LOAD_DYLIB && \
    echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
otool -l libclosure_smoketest.macho | grep -q LC_UNIXTHREAD || \
    echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
if nm -u libclosure_smoketest.macho | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u libclosure_smoketest.macho >&2
fi
