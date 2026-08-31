#!/bin/bash
# Builds libc_build/system/libcopyfile.dylib: static-to-dylib migration,
# docs/backlog.md item d8's "real, standalone libraries still static,
# genuine conversion candidates" list (docs/daemon-roadmap.md's
# "Static-vs-dylib architecture debt" section). Real install path
# confirmed against third_party/copyfile/xcodescripts/copyfile.xcconfig
# (`INSTALL_PATH = /usr/lib/system`, `PRODUCT_NAME = copyfile`).
#
# Real, confirmed dependency on libremovefile.dylib (build first, see
# build_removefile_dylib.sh): `nm` confirms libcopyfile.a's own objects
# reference fts_close/open/read/set$INODE64, geteuid, mkdir, qsort_b,
# unlink -- all real, only defined in libremovefile.a's own objects, not
# locally in libcopyfile.a. Linked as a real sibling dylib below, not
# re-bundled (avoids reproducing the exact duplicate-global-state class
# this whole migration exists to close).
#
# Real, unmodified Apple/FreeBSD source: reuses every already-built,
# already-verified member object out of libc_build/libcopyfile.a (ar x,
# not recompiled), plus one real, cheap additional file this archive's
# own narrower build never needed but a real dylib link surfaces as a
# genuine external reference (same real nm-based forensic pass
# build_libresolv_dylib.sh's own header documents): Libc/gen/FreeBSD/
# getcwd.c, for `__private_getcwd` (realpath.c's own real dependency;
# self-contained, standard headers only, same low-risk-addition bar as
# build_libresolv_dylib.sh's if_indextoname.c/herror.c/nsap_addr.c).
# getcwd.c's OTHER public symbol, plain `_getcwd`, is a real, full
# duplicate of libsystem_c.dylib's own real copy (already a linked
# sibling below) -- same "keep the object locally compiled for one
# private-use symbol, hide the other public name" shape already
# established for pread.o/pwrite.o's `$UNIX2003` aliases below, not a
# case where the whole object could just be dropped (this dylib's own
# `__private_getcwd` has no other real provider anywhere in this
# project). Hidden via `-Wl,-unexported_symbol,_getcwd` on the final
# link (2026-08-31, static-linking cleanup, dylib-vs-dylib duplicate-
# symbol pass, continuation).
#
# Real, honestly-classified `-Wl,-U` deferrals:
#   - __global_locale/__locale_key: SAME real gotcha build_libresolv_
#     dylib.sh hit first (see that script's own header) -- a real local
#     definition (libc_locale_globals_compat.c + table.o/none.o from
#     libc_locale.a) is required, `-Wl,-U` alone fails with a real ld64
#     `fixup error (kind=arm64_adrp_lo12)` for this exact symbol pair
#     (direct data-address computation, not a lazily-bound call).
#     Provided below, not deferred.
#   - chmod$UNIX2003/fchmod$UNIX2003/pread$UNIX2003/pwrite$UNIX2003/
#     getattrlist$UNIX2003/statfs$INODE64: CORRECTED 2026-08-29 -- NOT a
#     project-wide gap, not deferred here anymore. All six real base
#     symbols (chmod/fchmod/pread/pwrite/getattrlist/statfs) are
#     genuinely LOCAL to this exact archive/dylib (confirmed via
#     `nm -gU`) -- real, working `-Wl,-alias` entries below instead
#     (same real bug class already found+fixed for libremovefile.dylib's
#     fsync$UNIX2003/reallocarray$DARWIN_EXTSN, see that script's own
#     header for the full trail: a real launchd boot-time failure,
#     "symbol not found in flat namespace", surfaced this original
#     `-Wl,-U` as wrong).
set -e
cd "$(dirname "$0")"

OUT="libc_build"
EXTRACT="$OUT/libcopyfile_dylib/extracted"
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
if [ ! -f "$OUT/libcopyfile.a" ]; then
    echo "error: $OUT/libcopyfile.a not found -- run build_copyfile.sh first (this script reuses its already-built member objects)" >&2
    exit 1
fi
if [ ! -f "$OUT/libc_locale.a" ]; then
    echo "error: $OUT/libc_locale.a not found -- run build_libc_locale.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libremovefile.dylib" ]; then
    echo "error: $OUT/system/libremovefile.dylib not found -- run build_removefile_dylib.sh first" >&2
    exit 1
fi
for d in libsystem_c libsystem_malloc libsystem_platform libsystem_kernel libsystem_pthread libsystem_blocks; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2
        exit 1
    fi
done

DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
    exit 1
fi

(cd "$EXTRACT" && ar x "../../libcopyfile.a")
# getuid.o REMOVED (not from libcopyfile.a itself -- that archive stays
# intact, dyld.macho's own real static link in build_dyld_link.sh still
# needs it, and dyld runs before any dylib including this one exists)
# 2026-08-30 (static-linking cleanup, dylib-vs-dylib duplicate-symbol
# pass): once extracted into THIS dylib's own build, _getuid duplicates
# a real export libsystem_kernel.dylib already has (see
# build_libsystem_kernel.sh's "geteuid/egid"/create-syscalls.pl
# comment) -- this dylib already links libsystem_kernel.dylib as a
# sibling below, so dropping just this one extracted copy (a
# single-symbol raw syscall trap, confirmed via nm -m, no other member
# depends on it) lets libcopyfile.a's own undefined references resolve
# to the real sibling instead.
#
# pread.o/pwrite.o are NOT dropped the same way: this dylib's own final
# link below creates `_pread$UNIX2003`/`_pwrite$UNIX2003` as
# `-Wl,-alias` re-exports of these two objects' real `_pread`/`_pwrite`
# definitions, and docs/backlog.md's own ld64 finding (2026-08-2x
# session, "-Wl,-alias nonlocal-base bug") is that an alias's BASE must
# be a symbol genuinely compiled into THIS SAME link, or ld64 silently
# emits a malformed ordinal-0 re-export instead of erroring -- removing
# pread.o/pwrite.o and relying on the libsystem_kernel.dylib sibling for
# the base would trigger exactly that. Kept locally, but the PLAIN
# `_pread`/`_pwrite` names (the actual libsystem_kernel.dylib duplicate)
# are hidden from this dylib's export trie via `-Wl,-unexported_symbol`
# on the final link instead -- the alias step still sees a real local
# definition to point `_pread$UNIX2003`/`_pwrite$UNIX2003` at, but
# nothing outside this dylib can bind to the plain names here anymore.
rm -f "$EXTRACT/getuid.o"

# fpathconf.o REMOVED 2026-08-31 (static-linking cleanup, "needs a new
# home" batch): same real shape as getuid.o just above -- a single-
# symbol raw syscall trap (real, generated via create-syscalls.pl in
# build_copyfile.sh, confirmed via nm -m no other member of this extract
# dir defines it) that now has a real canonical home,
# libsystem_kernel.dylib (build_libsystem_kernel.sh's own new
# `_fpathconf` syscall-stub generation, same batch) -- this dylib
# already links libsystem_kernel.dylib as a sibling below, so
# copyfile.o's own undefined `_fpathconf` reference now resolves
# externally instead of to a second private copy. Not from
# libcopyfile.a itself -- that archive stays intact for dyld.macho's
# real static link.
rm -f "$EXTRACT/fpathconf.o"

# copy.o/parse.o/unparse.o/pack.o REMOVED 2026-08-31 (static-linking
# cleanup, libcopyfile-vs-libxpc ownership investigation): real Apple
# source (third_party/Libc/uuid/uuidsrc/*.c), same class of "genuinely
# part of Libc" duplicate as getuid.o above -- these 4 uuid_* objects
# are now the real, single-owner responsibility of libsystem_c.dylib
# (build_libsystem_c.sh extracts the SAME objects out of this dylib's
# own libcopyfile.a, see that file's own comment), which this dylib
# already links as a sibling below. Not from libcopyfile.a itself --
# that archive stays intact for dyld.macho's real static link.
rm -f "$EXTRACT/copy.o" "$EXTRACT/parse.o" "$EXTRACT/unparse.o" "$EXTRACT/pack.o"

# ftruncate.o/strtoul.o REMOVED 2026-08-31 (static-linking cleanup,
# dylib-vs-dylib duplicate-symbol pass, continuation): both real,
# unmodified Libc source, both full real duplicates of
# libsystem_c.dylib's own copies of the same source (already a linked
# sibling below).
#   - ftruncate.o defines ONLY `_ftruncate` (confirmed via nm); this
#     archive's one real consumer, copyfile.o's undefined reference to
#     `_ftruncate`, now resolves externally to the sibling instead.
#   - strtoul.o defines `_strtoul`/`_strtoul_l`. Removing it here does
#     NOT fully single-owner these two (libc++.dylib independently
#     compiles its own copy too, part of the same already-deferred
#     xlocale family docs/static-linking-cleanup-plan.md tracks under
#     DAR-159 -- confirmed via nm -m this dylib's strtoul.o is a THIRD,
#     genuinely redundant copy, not the one the xlocale wall is about),
#     but it is still a real, safe dedup: one fewer independently-
#     compiled copy of real mutable-free numeric-parsing code in the
#     process, and removes this dylib from that duplicate's owner set
#     entirely.
rm -f "$EXTRACT/ftruncate.o" "$EXTRACT/strtoul.o"

CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 \
    -Wno-nullability-completeness \
    -D_LIBC_NO_FEATURE_VERIFICATION -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 \
    -DVARIANT_STATIC -DVARIANT_CANCELABLE -DVARIANT_DARWINEXTSN -U__DARWIN_NON_CANCELABLE -D__DARWIN_NON_CANCELABLE=0 \
    -D__ct_rune_t=__darwin_ct_rune_t -D__va_list=__builtin_va_list \
    -include libc_build_prototypes.h \
    -I $OUT/compat_include \
    -I $LIBC/include -I $LIBC/darwin -I $LIBC/fbsdcompat -I $LIBC/locale -I $LIBC/locale/FreeBSD \
    -I $LIBC/stdtime/FreeBSD -I $LIBC/gen \
    -I $PTHREAD/include -I $PTHREAD/private \
    -I $XNU/bsd -I $XNU/osfmk -I $XNU/libsyscall \
    -I ../../generated"

echo "compiling 1 real additional file (getcwd.c for __private_getcwd)..."
"$CLANG" $CFLAGS "$LIBC/gen/FreeBSD/getcwd.c" -o "$EXTRACT/getcwd.o"

# copyfile_syscalls.o recompiled fresh here (NOT left as the `ar x`-
# extracted copy above) 2026-08-31 (static-linking cleanup, dylib-vs-
# dylib duplicate-symbol pass, BSD socket family): guards out chmod()
# (real canonical home now libsystem_kernel.dylib, see that script's
# own "BSD socket family" comment and copyfile_syscalls.c's own
# comment) while keeping every other real symbol in this file
# (fchmod()/getattrlist()/the mbr_*/getpwnam-family/syslog()/
# open_dprotected_np() -- libsystem_kernel.dylib doesn't have real
# copies of those (yet), so this dylib's own real implementations stay
# the correct, only copies).
"$CLANG" $CFLAGS -DLIBRESOLV_SOCKET_SYSCALLS_DYLIB_VARIANT=1 copyfile_syscalls.c -o "$EXTRACT/copyfile_syscalls.o"

# Real fix for __global_locale/__locale_key, same exact recipe
# build_libresolv_dylib.sh's own header/comment block established --
# copied here, not re-derived.
(cd "$EXTRACT" && ar x "../../libc_locale.a" table.o none.o)
# See narrow_rune_locale.sh's own header (static-linking cleanup, Phase
# 1d, 2026-08-30): demotes the 4 public rune-locale globals table.o/
# none.o carry so they bind to libsystem_c.dylib (a sibling below)
# instead of this dylib defining a second, independently-mutable copy.
./narrow_rune_locale.sh "$EXTRACT"
"$CLANG" $CFLAGS libc_locale_globals_compat.c -o "$EXTRACT/libc_locale_globals_compat.o"

mkdir -p "$OUT/system"
rm -f "$OUT/system/libcopyfile.dylib"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/system/libcopyfile.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -Wl,-alias,_fchmod,'_fchmod$UNIX2003' \
    `# _chmod/_chmod$UNIX2003 alias REMOVED 2026-08-31 (static-linking` \
    `# cleanup, BSD socket family pass): real base moved to` \
    `# libsystem_kernel.dylib (which creates its own _chmod$UNIX2003` \
    `# alias there, base genuinely local to that link); this dylib no` \
    `# longer defines plain _chmod locally (copyfile_syscalls.c's own` \
    `# LIBRESOLV_SOCKET_SYSCALLS_DYLIB_VARIANT guard), so re-creating` \
    `# this alias here would hit the documented ld64 "-alias nonlocal` \
    `# base" bug.` \
    -Wl,-alias,_pread,'_pread$UNIX2003' -Wl,-alias,_pwrite,'_pwrite$UNIX2003' \
    -Wl,-unexported_symbol,_pread -Wl,-unexported_symbol,_pwrite \
    -Wl,-unexported_symbol,_getcwd \
    -Wl,-alias,_getattrlist,'_getattrlist$UNIX2003' -Wl,-alias,_statfs,'_statfs$INODE64' \
    -o "$OUT/system/libcopyfile.dylib" \
    "$EXTRACT"/*.o \
    "$OUT/system/libremovefile.dylib" \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_pthread.dylib" "$OUT/system/libsystem_blocks.dylib" \
    2>"$OUT/libcopyfile_dylib_link.err" \
    && echo "wrote $OUT/system/libcopyfile.dylib" \
    || { echo "libcopyfile.dylib link found real undefined symbols:" >&2; grep '^  "' "$OUT/libcopyfile_dylib_link.err" | sed 's/,.*//' | sort -u >&2; exit 1; }

otool -hv "$OUT/system/libcopyfile.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
echo "declared -Wl,-U deferrals (expect: <flat-namespace> bind target):"
xcrun dyld_info -fixups "$OUT/system/libcopyfile.dylib" 2>/dev/null | grep '<flat-namespace>' | awk '{print "  " $NF}'
UNEXPECTED=$(xcrun dyld_info -fixups "$OUT/system/libcopyfile.dylib" 2>/dev/null | grep -v -E '<flat-namespace>|<weak-def-coalesce>' | grep 'bind' | grep -v -E 'libsystem_c/|libsystem_malloc/|libsystem_platform/|libsystem_kernel/|libsystem_pthread/|libsystem_blocks/|libremovefile/' || true)
if [ -n "$UNEXPECTED" ]; then
    echo "error: real, unexpected non-sibling bind targets found:" >&2
    echo "$UNEXPECTED" >&2
    exit 1
fi
echo "libcopyfile.dylib: every non-deferred external reference binds to a real, named sibling dylib"
