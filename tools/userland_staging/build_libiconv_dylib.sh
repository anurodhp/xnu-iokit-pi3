#!/bin/bash
# Builds libc_build/system/libiconv.dylib: static-to-dylib migration,
# docs/backlog.md item d8's "real, standalone libraries still static,
# genuine conversion candidates" list (docs/daemon-roadmap.md's
# "Static-vs-dylib architecture debt" section). Real Apple/GNU install
# path: /usr/lib/libiconv.2.dylib (real, versioned, top-level -- same
# class of standalone non-libsystem_* dylib as libresolv.9.dylib/
# libxml2.2.dylib, not part of the libSystem umbrella).
#
# Real, unmodified Apple/GNU source: reuses every already-built,
# already-verified member object out of libc_build/libiconv.a (ar x, not
# recompiled -- see build_libiconv.sh's own header for the full scope
# writeup, not re-derived here: heapsort.o/qsort.o real transitive
# FreeBSD deps, iconv.o/localcharset.o the real GNU libiconv/libcharset
# bodies, libiconv_flsl_compat.o this project's own small compat shim).
#
# Real, honestly-classified `-Wl,-U` gap, found the same way
# build_libresolv_dylib.sh's own header documents (extract every member,
# union defined vs undefined across the whole archive, subtract what the
# 5 sibling dylibs below actually export -- not guessed): only
# __global_locale/__locale_key remain (nl_langinfo/mbrtowc/wcrtomb/
# fopen/fclose/getc/ungetc/fscanf/malloc/free/etc are all real,
# confirmed exports of libsystem_c.dylib/libsystem_malloc.dylib).
#
# Real, known gotcha (found for real converting libresolv first, see
# that script's own header): __global_locale/__locale_key are referenced
# via a direct arm64 adrp/lo12 DATA-address fixup, not a lazily-bound
# call -- `-Wl,-U` alone produces a real `ld: fixup error
# (kind=arm64_adrp_lo12)` at link time. Real fix, same as
# build_libresolv_dylib.sh/build_libdispatch_dylib.sh: compile
# libc_locale_globals_compat.c (this project's own real, narrow
# reproduction of xlocale.c's two globals) + table.o/none.o (real
# locale/FreeBSD/table.c's _DefaultRuneXLocale + its own charset-
# conversion dependency, extracted from the already-built libc_locale.a)
# directly into this dylib as real local definitions.
set -e
cd "$(dirname "$0")"

OUT="libc_build"
EXTRACT="$OUT/libiconv_dylib/extracted"
rm -rf "$EXTRACT"
mkdir -p "$EXTRACT"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi
if [ ! -f "$OUT/libiconv.a" ]; then
    echo "error: $OUT/libiconv.a not found -- run build_libiconv.sh first (this script reuses its already-built member objects)" >&2
    exit 1
fi
for d in libsystem_c libsystem_malloc libsystem_platform libsystem_kernel libsystem_pthread; do
    if [ ! -f "$OUT/system/$d.dylib" ]; then
        echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2
        exit 1
    fi
done
if [ ! -f "$OUT/libc_locale.a" ] || [ ! -f "$OUT/compat_include/System/machine/cpu_capabilities.h" ]; then
    echo "error: $OUT/libc_locale.a or its compat_include/System/machine/cpu_capabilities.h not found -- run build_libc_locale.sh first" >&2
    exit 1
fi

# Real, verified fix (2026-08-28, see docs/backlog.md item d9 for the
# full trail): Xcode 12's own bundled ld64-609 has a real internal crash
# under -Wl,-fixup_chains that a much newer ld does not. Compiling still
# uses Xcode 12's clang/SDK above; only the final link step's linker
# binary changes, via clang's -B override.
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
if [ ! -x "$NEWLD_BINDIR/ld" ]; then
    echo "error: no usable 'ld' found under the daily-driver Xcode ($DAILY_XCODE) -- run 'xcode-select -p' to check, see docs/backlog.md item d9" >&2
    exit 1
fi

(cd "$EXTRACT" && ar x "../../libiconv.a")
(cd "$EXTRACT" && ar x "../../libc_locale.a" table.o none.o)

# Same real CFLAGS recipe every locale-touching compat file in this
# project uses (see build_libresolv_dylib.sh's own copy for the fuller
# citation of each -I/-D).
LIBC="../../third_party/Libc"
PTHREAD="../../third_party/libpthread"
XNU="../../third_party/xnu-7195"
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
rm -f "$OUT/system/libiconv.dylib"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/libiconv.2.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o "$OUT/system/libiconv.dylib" \
    "$EXTRACT"/*.o \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_malloc.dylib" \
    "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_kernel.dylib" \
    "$OUT/system/libsystem_pthread.dylib" \
    2>"$OUT/libiconv_dylib_link.err" \
    && echo "wrote $OUT/system/libiconv.dylib" \
    || { echo "libiconv.dylib link found real undefined symbols:" >&2; grep '^  "' "$OUT/libiconv_dylib_link.err" | sed 's/,.*//' | sort -u >&2; exit 1; }

otool -hv "$OUT/system/libiconv.dylib" | grep -q " DYLIB " || {
    echo "error: filetype is not DYLIB" >&2
    exit 1
}
# See build_libresolv_dylib.sh's own verification comment for why this
# checks dyld_info -fixups bind targets rather than plain `nm -u`
# emptiness (nm -u lists every non-locally-defined symbol regardless of
# whether it correctly binds to a linked sibling dylib).
UNEXPECTED=$(xcrun dyld_info -fixups "$OUT/system/libiconv.dylib" 2>/dev/null | grep -v -E '<flat-namespace>|<weak-def-coalesce>' | grep 'bind' | grep -v -E 'libsystem_c/|libsystem_malloc/|libsystem_platform/|libsystem_kernel/|libsystem_pthread/' || true)
if [ -n "$UNEXPECTED" ]; then
    echo "error: real, unexpected non-sibling bind targets found:" >&2
    echo "$UNEXPECTED" >&2
    exit 1
fi
echo "libiconv.dylib: every external reference binds to a real, named sibling dylib -- no unexpected/dangling references (no -Wl,-U deferrals needed for this one -- __global_locale/__locale_key resolved as real local definitions instead)"
