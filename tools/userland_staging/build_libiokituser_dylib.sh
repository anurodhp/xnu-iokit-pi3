#!/bin/bash
# Builds libc_build/system/libiokituser.dylib: static-to-dylib migration,
# docs/backlog.md item d8 -- last unit of this pass's Stage A batch.
# Real Apple ships this as part of IOKit.framework, not a plain dylib --
# this project has no framework-bundle packaging (same simplification
# d8's own note already flags), so this uses a plain, top-level
# /usr/lib/libiokituser.dylib install_name instead, matching every other
# non-libsystem_* standalone dylib in this migration (libresolv.9.dylib,
# libxml2.2.dylib, libiconv.2.dylib).
#
# Real, unmodified source: reuses every already-built member object out
# of libc_build/libiokituser.a (ar x, not recompiled -- see
# build_iokituser.sh's own header for the full scope: real
# iokitmig64.o/mach_portUser.o MIG client stubs, IODataQueueClient.o,
# iokit_trap.o, mach_port_wrappers_real.o/mach_port_extra_traps.o).
#
# Real gap closure, found via the same nm-based intra-archive resolution
# pass build_libresolv_dylib.sh's header documents: the only 3 truly-
# undefined symbols (mig_strncpy, mig_strncpy_zerofill,
# voucher_mach_msg_set) are ALL real, direct exports of this same
# migration pass's own libxpc.dylib (built earlier this pass, via its
# real liblaunchd_mig.a force_load + mach_ipc_dead_stubs_voucher_only.o)
# -- linked as a real sibling below instead of deferred. No `-Wl,-U`
# deferrals needed for this library at all.
set -e
cd "$(dirname "$0")"

OUT="libc_build"
EXTRACT="$OUT/libiokituser_dylib/extracted"
rm -rf "$EXTRACT"
mkdir -p "$EXTRACT"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
if [ ! -f "$OUT/libiokituser.a" ]; then
    echo "error: $OUT/libiokituser.a not found -- run build_iokituser.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libxpc.dylib" ]; then
    echo "error: $OUT/system/libxpc.dylib not found -- run build_libxpc_dylib.sh first (this script links it as a real sibling for mig_strncpy/voucher_mach_msg_set)" >&2
    exit 1
fi
for d in libsystem_c libsystem_malloc libsystem_platform libsystem_kernel libsystem_pthread; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2
        exit 1
    fi
done

# Real, verified fix (docs/backlog.md item d9): Xcode 12's own bundled
# ld64-609 has a real internal crash under -Wl,-fixup_chains that a much
# newer ld does not. Compiling still uses Xcode 12's clang/SDK above;
# only the final link step's linker binary changes, via clang's -B
# override.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
    exit 1
fi

(cd "$EXTRACT" && ar x "../../libiokituser.a")

mkdir -p "$OUT/system"
rm -f "$OUT/system/libiokituser.dylib"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/libiokituser.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o "$OUT/system/libiokituser.dylib" \
    "$EXTRACT"/*.o \
    "$OUT/system/libxpc.dylib" \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_pthread.dylib" \
    2>"$OUT/libiokituser_dylib_link.err" \
    && echo "wrote $OUT/system/libiokituser.dylib" \
    || { echo "libiokituser.dylib link found real undefined symbols:" >&2; grep '^  "' "$OUT/libiokituser_dylib_link.err" | sed 's/,.*//' | sort -u >&2; exit 1; }

otool -hv "$OUT/system/libiokituser.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
echo "declared -Wl,-U deferrals (expect: none for this library):"
xcrun dyld_info -fixups "$OUT/system/libiokituser.dylib" 2>/dev/null | grep '<flat-namespace>' | awk '{print "  " $NF}'
UNEXPECTED=$(xcrun dyld_info -fixups "$OUT/system/libiokituser.dylib" 2>/dev/null | grep -v -E '<flat-namespace>|<weak-def-coalesce>' | grep 'bind' | grep -v -E 'libsystem_c/|libsystem_malloc/|libsystem_platform/|libsystem_kernel/|libsystem_pthread/|libxpc/' || true)
if [ -n "$UNEXPECTED" ]; then
    echo "error: real, unexpected non-sibling bind targets found:" >&2
    echo "$UNEXPECTED" >&2
    exit 1
fi
echo "libiokituser.dylib: every external reference binds to a real, named sibling dylib -- no unexpected/dangling references"
