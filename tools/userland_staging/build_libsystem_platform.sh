#!/bin/bash
# Builds libc_build/system/libsystem_platform.dylib: static-to-dylib
# migration batch 1a (docs/daemon-roadmap.md's "Static-vs-dylib
# architecture debt" section), following build_libsystem_kernel.sh's
# established pattern. Real, unmodified Apple source, same files
# build_libplatform.sh already compiles for its own static libplatform.a
# (os_unfair_lock/os_once/OSAtomic, this project's own real Mach-trap/
# raw-syscall stub files for the two traps/two syscalls the BSD-syscall
# Perl generator doesn't cover -- see build_libplatform.sh's own header
# comment for the full real-gap writeup, not re-derived here) -- same
# CFLAGS, same file list, just linked as a real -dynamiclib instead of
# `ar`. Audited before writing this script: no process-global mutable
# state here needs cross-image sharing the way mach_task_self_ did
# (os_unfair_lock/OSAtomic operate on caller-provided memory; TSD reads
# TPIDRRO_EL0 directly, not an image-local global) -- confirmed by
# reading lock.c/atomic.c/OSAtomic.c, not assumed.
#
# Also folds in os/alloc_once.c, string/generic/bzero.c, string/generic/
# memset_pattern.c -- real libplatform source that build_libdyld.sh/
# build_libsystem.sh previously each compiled as their OWN loose, private
# copies (build_libplatform.sh's own file list never included them, see
# that script's header comment on why) rather than duplicated a third
# time here. This one's a real architectural cleanup, not a correctness
# fix like mach_task_self_ was -- os_alloc_once's own per-image
# _os_alloc_heap bump-pointer global stays independent per caller-site
# either way (each os_alloc_once() slot is its own independent bump
# region regardless of which image's copy of the code runs), so
# duplicating it was wasteful, not unsafe.
set -e
cd "$(dirname "$0")"

PLATFORM="../../third_party/libplatform"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
OUT="libc_build"
mkdir -p "$OUT/libsystem_platform/apple_platform"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libsystem_kernel.dylib" ]; then
    echo "error: $OUT/system/libsystem_kernel.dylib not found -- run build_libsystem_kernel.sh first (real errno/cerror/mach-trap dependency)" >&2
    exit 1
fi
if [ ! -f "$OUT/compat_include/mach_reply_port_decl.h" ]; then
    echo "error: $OUT/compat_include/mach_reply_port_decl.h not found -- run build_libmalloc.sh first" >&2
    exit 1
fi

# Identical to build_libplatform.sh's own CFLAGS (not re-derived --
# same real header graph).
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness -Wno-availability -Wno-deprecated-declarations \
    -D__API_AVAILABLE_PLATFORM_bridgeos(x)=ios,introduced=x \
    -DPRIVATE=1 -DSWITCH_OPTION_OSLOCK_DEPRESS=4 -DSWITCH_OPTION_OSLOCK_WAIT=5 \
    -I $PLATFORM/internal -I $PLATFORM/private -I $PLATFORM/include -I $PLATFORM/src \
    -I $PLATFORM/src/os/resolver \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/libsyscall -I $XNU/bsd \
    -I $OUT/compat_include \
    -idirafter $XNU/osfmk -idirafter $XNU/libkern"

ok=0
skip=0
for f in "$PLATFORM/src/os/lock.c" "$PLATFORM/src/os/atomic.c" "$PLATFORM/src/atomics/arm64/OSAtomic.c" \
         "$PLATFORM/src/os/alloc_once.c" "$PLATFORM/src/string/generic/bzero.c" "$PLATFORM/src/string/generic/memset_pattern.c" \
         "$PLATFORM/src/string/generic/memmove.c"; do
    base=$(basename "$f" .c)
    out="$OUT/libsystem_platform/apple_platform/${base}.o"
    if "$CLANG" $CFLAGS "$f" -o "$out" 2>"$OUT/libsystem_platform/apple_platform/${base}.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/libsystem_platform/apple_platform/${base}.err"
    else
        skip=$((skip + 1))
        echo "  skip: $base"
    fi
done
echo "apple_platform: $ok compiled, $skip skipped"

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c libplatform_mach_traps.s -o "$OUT/libsystem_platform/libplatform_mach_traps.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -DPRIVATE=1 \
    -I "$OUT/compat_include" -idirafter "$XNU/osfmk" \
    "$XNU/libsyscall/wrappers/mach_absolute_time.s" -o "$OUT/libsystem_platform/mach_absolute_time.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -DPRIVATE=1 -I "$OUT/compat_include" -idirafter "$XNU/osfmk" \
    "$XNU/libsyscall/wrappers/mach_timebase_info.c" -o "$OUT/libsystem_platform/mach_timebase_info.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    libplatform_syscalls.c -o "$OUT/libsystem_platform/libplatform_syscalls.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -I "$XNU/libsyscall" libplatform_thread_self.c -o "$OUT/libsystem_platform/libplatform_thread_self.o"

mkdir -p "$OUT/system"
rm -f "$OUT/system/libsystem_platform.dylib"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/system/libsystem_platform.dylib \
    -Wl,-fixup_chains \
    -o "$OUT/system/libsystem_platform.dylib" \
    "$OUT/libsystem_platform/apple_platform"/*.o \
    "$OUT/libsystem_platform/libplatform_mach_traps.o" "$OUT/libsystem_platform/mach_absolute_time.o" \
    "$OUT/libsystem_platform/mach_timebase_info.o" "$OUT/libsystem_platform/libplatform_syscalls.o" \
    "$OUT/libsystem_platform/libplatform_thread_self.o" \
    "$OUT/system/libsystem_kernel.dylib"

echo "wrote $OUT/system/libsystem_platform.dylib"
otool -hv "$OUT/system/libsystem_platform.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
if nm -u "$OUT/system/libsystem_platform.dylib" | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u "$OUT/system/libsystem_platform.dylib" >&2
else
    echo "libsystem_platform.dylib: fully self-contained (beyond libsystem_kernel.dylib), zero undefined symbols"
fi
