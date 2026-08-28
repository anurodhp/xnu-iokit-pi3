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
if [ ! -f "$OUT/system/libsystem_platform.dylib" ]; then
    echo "error: $OUT/system/libsystem_platform.dylib not found -- run build_libsystem_platform.sh first (real _platform_memmove dependency)" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libsystem_malloc.dylib" ]; then
    echo "error: $OUT/system/libsystem_malloc.dylib not found -- run build_libsystem_malloc.sh first (real malloc/free dependency, see below)" >&2
    exit 1
fi

CFLAGS_COMMON="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -I $CLOSURE -I $PLATFORM/private -I $OUT/compat_include"

"$CLANG" $CFLAGS_COMMON "$CLOSURE/data.c" -o "$OUT/libsystem_blocks/data.o"
"$CLANG" $CFLAGS_COMMON -x c++ -std=gnu++14 -fno-exceptions -fno-rtti \
    '-Dos_assumes(x)=(x)' \
    '-Dos_assert(x)=((x)?(void)0:__builtin_trap())' \
    "$CLOSURE/runtime.cpp" -o "$OUT/libsystem_blocks/runtime.o"

# _malloc/_free (real Block_copy/Block_release heap-promotion calls):
# real LC_LOAD_DYLIB dependency on libsystem_malloc.dylib as of
# migration step 3 (docs/daemon-roadmap.md) -- this used to be a
# temporary -Wl,-U flat-namespace lookup, upgraded now that
# libsystem_malloc.dylib is real.
mkdir -p "$OUT/system"
rm -f "$OUT/system/libsystem_blocks.dylib"
    # -not_for_dyld_shared_cache: real, newly-surfaced requirement of
    # the daily-driver Xcode's newer ld (NEWLD_BINDIR above) -- it
    # refuses "-undefined dynamic_lookup"/-U on a dylib it classifies
    # as shared-cache eligible (real /usr/lib install path) unless this
    # is set, a real check Xcode 12's ld64-609 never enforced. This
    # project builds no real dyld shared cache at all, so opting every
    # real dylib out is correct, not a workaround (docs/backlog.md item
    # d9's NEWLD_BINDIR sweep, 2026-08-28).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/system/libsystem_blocks.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o "$OUT/system/libsystem_blocks.dylib" \
    "$OUT/libsystem_blocks/data.o" "$OUT/libsystem_blocks/runtime.o" \
    "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_malloc.dylib"

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
