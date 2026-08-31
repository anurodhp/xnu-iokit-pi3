#!/bin/bash
# Builds libc_build/system/libncurses.dylib: static-to-dylib migration,
# docs/backlog.md item d8 -- confirmed real, unmodified ncurses source
# builds a versioned, infix-style dylib (`libncurses.<ver>.dylib` --
# third_party/ncurses/ncurses/aclocal.m4:4170's own comment: "version
# numbers are infix (ex: libncurses.<ver>.dylib)"). Real install_name:
# /usr/lib/libncurses.5.4.dylib, matching every real macOS system's own
# actual shipped path for this library -- this checkout's own
# dist.mk reports upstream version 5.7 (NCURSES_MAJOR/MINOR), which is
# NOT the same number as Apple's own dylib compatibility-version scheme
# for this library (Apple's own numbering is chosen independently of
# upstream's release version); noted here rather than silently
# presenting the upstream 5.7 as if it were verified from this
# checkout -- it isn't, this is the one real value in this script not
# directly re-derivable from the pinned source tree itself.
#
# Real, unmodified source: reuses every already-built member object out
# of libc_build/libncurses.a (ar x, not recompiled -- see build_ncurses.sh's
# own header for the full scope), plus real fixes for two genuine gaps a
# real dylib link surfaces that the archive's own narrower smoketest
# never needed:
#   - __global_locale/__locale_key: SAME real gap + SAME real fix as
#     build_libresolv_dylib.sh's own pilot (see that script's header for
#     the full citation) -- `-Wl,-U` cannot defer these (real arm64
#     adrp/lo12 direct data-address fixup, confirmed there already), so
#     this dylib also compiles libc_locale_globals_compat.c plus
#     table.o/none.o (extracted from libc_locale.a) as real local
#     definitions, verbatim, not re-derived.
#   - g_mach_ipc_clock_port/g_mach_ipc_clock_sem/mach_ipc_clock_init:
#     real, already-existing source (mach_ipc_clock_init.c, already
#     compiled by build_libmach_ipc.sh) -- ncurses' own napms()-family
#     delay support goes through this project's real
#     simple_cmds_nanosleep_pub_compat.o (already part of libncurses.a),
#     which needs these two real globals + the init function as real
#     local definitions (same "must be local, not -Wl,-U" reasoning as
#     the locale globals above -- these are also directly-addressed data
#     symbols). mach_ipc_clock_init.o extracted from the already-built
#     libmach_ipc.a, not recompiled.
#
# Real, honestly-classified `-Wl,-U` deferrals: clock_get_time,
# mach_host_self, host_get_clock_service, semaphore_create -- real Mach
# trap/MIG client routines (function calls, not data symbols, so
# `-Wl,-U` is valid here unlike the two data-symbol cases above),
# confirmed genuinely absent from every sibling dylib below via
# `nm -gU` (found via a real link attempt, not guessed preemptively --
# the first three are mach_ipc_clock_init.c's own real setup path for
# the two data globals above; without them, that init function would
# just no-op/return early at runtime, same "no real clock service
# reachable" tolerance as the rest of this file's own real code).
# simple_cmds_nanosleep_pub_compat.c's own real fallback path already
# handles a failed clock_get_time call, so this is a real, already-
# tolerated gap, not a new one.
set -e
cd "$(dirname "$0")"

OUT="libc_build"
EXTRACT="$OUT/libncurses_dylib/extracted"
rm -rf "$EXTRACT"
mkdir -p "$EXTRACT"

LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
if [ ! -f "$OUT/libncurses.a" ]; then
    echo "error: $OUT/libncurses.a not found -- run build_ncurses.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/libc_locale.a" ]; then
    echo "error: $OUT/libc_locale.a not found -- run build_libc_locale.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/libmach_ipc.a" ]; then
    echo "error: $OUT/libmach_ipc.a not found -- run build_libmach_ipc.sh first" >&2
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

(cd "$EXTRACT" && ar x "../../libncurses.a")
(cd "$EXTRACT" && ar x "../../libc_locale.a" table.o none.o)
# See narrow_rune_locale.sh's own header (static-linking cleanup, Phase
# 1d, 2026-08-30): demotes the 4 public rune-locale globals table.o/
# none.o carry so they bind to libsystem_c.dylib (a sibling below)
# instead of this dylib defining a second, independently-mutable copy.
./narrow_rune_locale.sh "$EXTRACT"
(cd "$EXTRACT" && ar x "../../libmach_ipc.a" mach_ipc_clock_init.o)

CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -DVARIANT_DARWINEXTSN -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/osfmk -I $XNU/libsyscall \
    -I ../../generated"
"$CLANG" $CFLAGS libc_locale_globals_compat.c -o "$EXTRACT/libc_locale_globals_compat.o"

mkdir -p "$OUT/system"
rm -f "$OUT/system/libncurses.dylib"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/libncurses.5.4.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -Wl,-U,_clock_get_time -Wl,-U,_mach_host_self \
    -Wl,-U,_host_get_clock_service -Wl,-U,_semaphore_create \
    -o "$OUT/system/libncurses.dylib" \
    "$EXTRACT"/*.o \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_pthread.dylib" \
    2>"$OUT/libncurses_dylib_link.err" \
    && echo "wrote $OUT/system/libncurses.dylib" \
    || { echo "libncurses.dylib link found real undefined symbols:" >&2; grep '^  "' "$OUT/libncurses_dylib_link.err" | sed 's/,.*//' | sort -u >&2; exit 1; }

otool -hv "$OUT/system/libncurses.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
echo "declared -Wl,-U deferrals (expect: <flat-namespace> bind target):"
xcrun dyld_info -fixups "$OUT/system/libncurses.dylib" 2>/dev/null | grep '<flat-namespace>' | awk '{print "  " $NF}'
UNEXPECTED=$(xcrun dyld_info -fixups "$OUT/system/libncurses.dylib" 2>/dev/null | grep -v -E '<flat-namespace>|<weak-def-coalesce>' | grep 'bind' | grep -v -E 'libsystem_c/|libsystem_malloc/|libsystem_platform/|libsystem_kernel/|libsystem_pthread/' || true)
if [ -n "$UNEXPECTED" ]; then
    echo "error: real, unexpected non-sibling bind targets found:" >&2
    echo "$UNEXPECTED" >&2
    exit 1
fi
echo "libncurses.dylib: every non-deferred external reference binds to a real, named sibling dylib -- no unexpected/dangling references"
