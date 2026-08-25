#!/bin/bash
# Builds libc_build/system/libsystem_blocks.dylib: static-to-dylib
# migration batch 1b (docs/daemon-roadmap.md's "Static-vs-dylib
# architecture debt" section) -- the real Apple libclosure (Blocks
# runtime) source, same files/flags build_libclosure.sh already
# established for its own static libclosure.a (see that script's own
# header comment for the full real-gap writeup on os_assumes/os_assert
# and why -fno-exceptions/-fno-rtti is real and correct here, not
# re-derived). Audited before writing this script: no process-global
# mutable state needs cross-image sharing here either -- refcounting
# operates on the caller-provided block object itself, and the default
# retain/release/destructInstance callbacks are only ever installed by
# a real ObjC runtime, which this project doesn't have (each dylib's
# copy just independently sits at real Apple's own defaults).
#
# Unlike build_libclosure.sh's own static libclosure.a (which bundles
# its own private copy of _platform_memmove, real but redundant), this
# dylib links against real libsystem_platform.dylib for it instead --
# matching real Apple's actual libsystem_blocks.dylib -> libsystem_platform.dylib
# dependency, and this project's own new build_libsystem_platform.sh
# already builds and exports it.
set -e
cd "$(dirname "$0")"

CLOSURE="../../third_party/libclosure"
PLATFORM="../../third_party/libplatform"
OUT="libc_build"
mkdir -p "$OUT/libsystem_blocks"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libsystem_platform.dylib" ]; then
    echo "error: $OUT/system/libsystem_platform.dylib not found -- run build_libsystem_platform.sh first (real _platform_memmove dependency)" >&2
    exit 1
fi

CFLAGS_COMMON="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -I $CLOSURE -I $PLATFORM/private -I $OUT/compat_include"

"$CLANG" $CFLAGS_COMMON "$CLOSURE/data.c" -o "$OUT/libsystem_blocks/data.o"
"$CLANG" $CFLAGS_COMMON -x c++ -std=gnu++14 -fno-exceptions -fno-rtti \
    '-Dos_assumes(x)=(x)' \
    '-Dos_assert(x)=((x)?(void)0:__builtin_trap())' \
    "$CLOSURE/runtime.cpp" -o "$OUT/libsystem_blocks/runtime.o"

# Real gap, temporary: _malloc/_free (real Block_copy/Block_release
# heap-promotion calls) aren't resolvable against a real dylib yet --
# libmalloc.a isn't migrated to libsystem_malloc.dylib until batch 2
# (see docs/daemon-roadmap.md). Marked as an intentional dynamic lookup
# (same real -Wl,-U mechanism _dyld_initializer already uses) rather than
# bundling yet another private static copy -- resolves via dyld's flat-
# namespace search against whichever loaded image currently exports
# them (today: libdyld.dylib/libSystem.B.dylib's own still-static
# libmalloc.a). Batch 2 should replace this with a real LC_LOAD_DYLIB
# dependency on libsystem_malloc.dylib once it exists, the same upgrade
# build_libsystem_kernel.sh's own consumers got from -Wl,-U to a real
# dependency in step 1.
mkdir -p "$OUT/system"
rm -f "$OUT/system/libsystem_blocks.dylib"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/system/libsystem_blocks.dylib \
    -Wl,-fixup_chains \
    -Wl,-U,_malloc -Wl,-U,_free \
    -o "$OUT/system/libsystem_blocks.dylib" \
    "$OUT/libsystem_blocks/data.o" "$OUT/libsystem_blocks/runtime.o" \
    "$OUT/system/libsystem_platform.dylib"

echo "wrote $OUT/system/libsystem_blocks.dylib"
otool -hv "$OUT/system/libsystem_blocks.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
if nm -u "$OUT/system/libsystem_blocks.dylib" | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u "$OUT/system/libsystem_blocks.dylib" >&2
else
    echo "libsystem_blocks.dylib: fully self-contained (beyond libsystem_platform.dylib), zero undefined symbols"
fi
