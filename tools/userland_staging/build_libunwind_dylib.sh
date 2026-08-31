#!/bin/bash
# Builds libc_build/system/libunwind.dylib: static-to-dylib migration
# step 6 (docs/daemon-roadmap.md's "Static-vs-dylib architecture debt"
# section) -- the last piece libc++.dylib was still linking as a static
# archive (see build_libcxx_dylib.sh's own header comment).
#
# Real, narrow correctness note, not treated as blocking (already a
# pre-existing, documented gap): libunwind's real DwarfFDECache
# (libunwind.o) is real, meaningful per-process cache state for
# registered dynamic FDEs. A real, separate libunwind.dylib means this
# cache is now shared correctly across every consumer that links this
# one real image (matching real Apple's own architecture) rather than
# libc++.dylib's own private copy from before -- a real improvement,
# not a new risk. The already-documented "throw/catch across a real
# DWARF-CFI phase-1 search" gap (Phase 6.5 Phase C, see
# build_llvm_libcxxabi.sh's own header comment) is unaffected either
# way.
#
# Real, unmodified LLVM source: same file set build_libunwind.sh already
# established for its own static archive (that script's own header
# comment has the full scope/gap writeup, not re-derived here) --
# reused by extracting already-built member objects (ar x).
#
# Real, honestly-scoped exclusion: libunwind_dyld_shim.o needs
# __mh_execute_header, never synthesized for MH_DYLIB output (same real
# reason build_dyld_link.sh/build_libdyld.sh/build_libcxx_dylib.sh
# already exclude it) -- this dylib provides its own real
# _dyld_find_unwind_sections/_dyld_register_func_for_remove_image/dladdr
# stubs instead (same real pattern build_libcxx_dylib.sh's own
# libcxx_dylib_compat.c already established, copied rather than
# depending on libc++.dylib for them, which would be circular --
# libc++.dylib depends on libunwind.dylib, not the other way around).
set -e
cd "$(dirname "$0")"

OUT="libc_build"
EXTRACT="$OUT/libunwind_dylib/extracted"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
CLANGXX="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANGXX" ] || [ ! -d "$SDK" ]; then
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
if [ ! -f "$OUT/libunwind.a" ]; then
    echo "error: $OUT/libunwind.a not found -- run build_libunwind.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libsystem_kernel.dylib" ] || [ ! -f "$OUT/system/libsystem_platform.dylib" ] || \
   [ ! -f "$OUT/system/libsystem_malloc.dylib" ] || [ ! -f "$OUT/system/libsystem_pthread.dylib" ] || \
   [ ! -f "$OUT/system/libsystem_c.dylib" ]; then
    echo "error: libsystem_kernel/platform/malloc/pthread/c.dylib not found -- run those build scripts first" >&2
    exit 1
fi

rm -rf "$EXTRACT"
mkdir -p "$EXTRACT"
(cd "$EXTRACT" && ar x "../../libunwind.a")
rm -f "$EXTRACT/__.SYMDEF"* "$EXTRACT/libunwind_dyld_shim.o"

# Removed 2026-08-30 (static-linking cleanup, Phase 1d): three
# `-Wl,-alias` entries used to re-export
# `__pthread_rwlock_{rdlock,wrlock,unlock}` here, aliased to the real
# `$UNIX2003` bodies. No object in this extract dir ever defined the
# plain double-underscore name -- `dyld_info -exports` confirmed each
# alias landed as a real `[re-export]` of `libsystem_pthread`'s own copy,
# and `nm -gUj libsystem_pthread.dylib` confirms `libsystem_pthread`
# ALREADY exports the exact same plain name directly. So this dylib was
# a second, redundant owner of a name its own sibling on the link line
# below already provides -- any real caller resolves it via ordinary
# two-level-namespace lookup with no alias needed at all, same
# conclusion build_libsystem_c.sh's own header reached when it dropped
# an equivalent dead `__pthread_cond_wait` alias for the same reason.

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    libcxx_dylib_compat.c -o "$EXTRACT/compat.o"

mkdir -p "$OUT/system"
rm -f "$OUT/system/libunwind.dylib"
    # -not_for_dyld_shared_cache: real, newly-surfaced requirement of
    # the daily-driver Xcode's newer ld (NEWLD_BINDIR above) -- it
    # refuses "-undefined dynamic_lookup"/-U on a dylib it classifies
    # as shared-cache eligible (real /usr/lib install path) unless this
    # is set, a real check Xcode 12's ld64-609 never enforced. This
    # project builds no real dyld shared cache at all, so opting every
    # real dylib out is correct, not a workaround (docs/backlog.md item
    # d9's NEWLD_BINDIR sweep, 2026-08-28).
"$CLANGXX" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/system/libunwind.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o "$OUT/system/libunwind.dylib" \
    "$EXTRACT"/*.o \
    "$OUT/system/libsystem_kernel.dylib" "$OUT/system/libsystem_platform.dylib" \
    "$OUT/system/libsystem_malloc.dylib" "$OUT/system/libsystem_pthread.dylib" "$OUT/system/libsystem_c.dylib"

echo "wrote $OUT/system/libunwind.dylib"
otool -hv "$OUT/system/libunwind.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
if nm -u "$OUT/system/libunwind.dylib" | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u "$OUT/system/libunwind.dylib" >&2
else
    echo "libunwind.dylib: fully self-contained (beyond libsystem_*.dylib), zero undefined symbols"
fi
