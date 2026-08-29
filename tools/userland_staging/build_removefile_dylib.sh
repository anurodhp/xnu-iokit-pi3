#!/bin/bash
# Builds libc_build/system/libremovefile.dylib: static-to-dylib
# migration, docs/backlog.md item d8's "real, standalone libraries still
# static, genuine conversion candidates" list (docs/daemon-roadmap.md's
# "Static-vs-dylib architecture debt" section). Real install path
# confirmed against third_party/removefile/xcodescripts/
# removefile.xcconfig (`INSTALL_PATH = /usr/lib/system`,
# `PRODUCT_NAME = removefile`) -- real Apple ships this as a libSystem-
# umbrella sub-dylib, not a top-level /usr/lib/ one, same as
# libcopyfile.dylib (see build_copyfile_dylib.sh, which links against
# THIS dylib -- real, confirmed dependency direction via `nm`:
# libcopyfile.a's fts_close/open/read/set$INODE64, geteuid, mkdir,
# qsort_b, unlink references all resolve to libremovefile.a's own
# objects; libremovefile.a references nothing from libcopyfile.a).
#
# Real, unmodified Apple/FreeBSD source: reuses every already-built,
# already-verified member object out of libc_build/libremovefile.a (ar
# x, not recompiled -- see build_removefile.sh's own header for the
# original file-list scope writeup).
#
# Real, honestly-classified `-Wl,-U` deferrals (2 symbols, found via the
# same real nm-based intra-archive-resolution forensic pass
# build_libresolv_dylib.sh's own header documents: extract every member
# object, union their defined symbols, subtract from the union of their
# undefined refs to get the TRUE cross-archive-boundary gap, then
# subtract every symbol the sibling dylibs below actually export):
#   - fsync$UNIX2003/reallocarray$DARWIN_EXTSN: CORRECTED 2026-08-29 --
#     these are NOT a project-wide gap, and not deferred here anymore.
#     The real base symbols (fsync/reallocarray) are genuinely LOCAL to
#     this exact archive/dylib (confirmed via `nm -g`) -- real, working
#     `-Wl,-alias` entries below instead (found while wiring
#     build_libdyld.sh to reexport this dylib -- a real launchd boot
#     failure surfaced the original `-Wl,-U` here as wrong, see this
#     script's own header for the full trail).
set -e
cd "$(dirname "$0")"

OUT="libc_build"
EXTRACT="$OUT/libremovefile_dylib/extracted"
rm -rf "$EXTRACT"
mkdir -p "$EXTRACT"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
if [ ! -f "$OUT/libremovefile.a" ]; then
    echo "error: $OUT/libremovefile.a not found -- run build_removefile.sh first (this script reuses its already-built member objects)" >&2
    exit 1
fi
for d in libsystem_c libsystem_malloc libsystem_platform libsystem_kernel libsystem_pthread libsystem_blocks; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2
        exit 1
    fi
done

# Real, verified fix (2026-08-28, see docs/backlog.md item d9 for the
# full trail): Xcode 12's own bundled ld64-609 has a real internal crash
# under -Wl,-fixup_chains that a much newer ld does not.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
    exit 1
fi

(cd "$EXTRACT" && ar x "../../libremovefile.a")

mkdir -p "$OUT/system"
rm -f "$OUT/system/libremovefile.dylib"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/system/libremovefile.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -Wl,-alias,_fsync,'_fsync$UNIX2003' -Wl,-alias,_reallocarray,'_reallocarray$DARWIN_EXTSN' \
    -o "$OUT/system/libremovefile.dylib" \
    "$EXTRACT"/*.o \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_pthread.dylib" "$OUT/system/libsystem_blocks.dylib" \
    2>"$OUT/libremovefile_dylib_link.err" \
    && echo "wrote $OUT/system/libremovefile.dylib" \
    || { echo "libremovefile.dylib link found real undefined symbols:" >&2; grep '^  "' "$OUT/libremovefile_dylib_link.err" | sed 's/,.*//' | sort -u >&2; exit 1; }

otool -hv "$OUT/system/libremovefile.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
# Real check (NOT plain `nm -u`, which lists every non-locally-defined
# symbol whether or not it correctly binds to a linked sibling dylib --
# see build_libresolv_dylib.sh's own verification comment for the full
# real finding): `dyld_info -fixups` shows declared `-Wl,-U` deferrals
# bound `<flat-namespace>` (resolved at real load time) and everything
# else bound to a real, named sibling dylib.
echo "declared -Wl,-U deferrals (expect: <flat-namespace> bind target):"
xcrun dyld_info -fixups "$OUT/system/libremovefile.dylib" 2>/dev/null | grep '<flat-namespace>' | awk '{print "  " $NF}'
UNEXPECTED=$(xcrun dyld_info -fixups "$OUT/system/libremovefile.dylib" 2>/dev/null | grep -v -E '<flat-namespace>|<weak-def-coalesce>' | grep 'bind' | grep -v -E 'libsystem_c/|libsystem_malloc/|libsystem_platform/|libsystem_kernel/|libsystem_pthread/|libsystem_blocks/' || true)
if [ -n "$UNEXPECTED" ]; then
    echo "error: real, unexpected non-sibling bind targets found:" >&2
    echo "$UNEXPECTED" >&2
    exit 1
fi
echo "libremovefile.dylib: every non-deferred external reference binds to a real, named sibling dylib"
