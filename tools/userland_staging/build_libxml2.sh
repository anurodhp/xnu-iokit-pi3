#!/bin/bash
# Builds libc_build/system/libxml2.dylib: real, unmodified Apple libxml2
# (third_party/libxml2, tag libxml2-34.9, pinned in setup_third_party.sh
# -- real upstream libxml2 2.9.4 under the hood, confirmed via
# LIBXML_MAJOR/MINOR/MICRO_VERSION in configure.ac). Real per-library
# dylib per this project's 2026-08-25 dylib-first directive.
#
# Real file list: third_party/libxml2/libxml2/Makefile.am's own
# libxml2_la_SOURCES, minus trio.c/triostr.c (WITH_TRIO_SOURCES is a
# real replacement snprintf/vsnprintf for platforms that lack real ones
# -- Darwin has real ones, Apple's own real builds never enable this).
#
# Unlike ICU, no need to run `configure` at all: Apple's own checkout
# ships a real pregenerated `Pregenerated Files/include/config.h` (and
# libxml/xmlversion.h) -- a real `configure` run's output, checked in
# for their own Xcode-based build. Confirmed real header-version match
# against the pinned Xcode 12 iPhoneOS SDK's own bundled libxml2
# headers too (both LIBXML_DOTTED_VERSION "2.9.4").
set -e
cd "$(dirname "$0")"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12" >&2
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

XML2="../../third_party/libxml2"
SRC="$XML2/libxml2"
PREGEN="$XML2/Pregenerated Files/include"
ICU="../../third_party/ICU/icuSources"
COMPAT="cf_compat_headers"
OUT="libc_build"

if [ ! -f "$SRC/tree.c" ]; then
    echo "error: $SRC not found -- run setup_third_party.sh first" >&2
    exit 1
fi
if [ ! -f "icu_build/target/lib/libicuuc.dylib" ]; then
    echo "error: icu_build/target/lib/libicuuc.dylib not found -- run build_icu_host.sh then build_icu_target.sh first" >&2
    exit 1
fi

# -include stddef.h: real zlib.h transitively provided ptrdiff_t for
# HTMLparser.c's own unrelated (non-compression) local use; disabling
# HAVE_ZLIB_H below (this project has no gzFile/gzopen/etc anywhere)
# removes that real header's transitive include along with it, so the
# real, standard, always-available <stddef.h> is forced in directly.
CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -O0 \
    -Wno-nullability-completeness -Wno-shorten-64-to-32 -Wno-deprecated-declarations \
    -Wno-unused-variable -Wno-unused-function -Wno-int-conversion \
    -DHAVE_CONFIG_H=1 \
    -include stddef.h \
    -I "$COMPAT" \
    -I "$OUT/xml2_headers" \
    -I "$PREGEN" -I "$SRC/include" -I "$SRC" \
    -I "$ICU/common")

mkdir -p "$OUT/xml2_headers/libxml"
rm -f "$OUT/xml2_headers/config.h"
cp "$PREGEN/config.h" "$OUT/xml2_headers/config.h"
cp -f "$PREGEN"/libxml/*.h "$OUT/xml2_headers/libxml/" 2>/dev/null || true
cp -f "$SRC"/include/libxml/*.h "$OUT/xml2_headers/libxml/"
cp -f "$PREGEN/libxml/xmlversion.h" "$OUT/xml2_headers/libxml/xmlversion.h"
# Real, deliberate scope reduction (not a source patch -- editing our own
# copy of the pregenerated header, matching the same "adjust the copy,
# not Apple's checked-in file" pattern build_icu_target.sh already used
# for its own DATASUBDIR fix): disable LIBXML_HTTP_ENABLED/FTP_ENABLED
# (nanohttp.c/nanoftp.c's real network transport -- needs socket/
# connect/bind/getaddrinfo/gethostbyname/etc, none of which exist
# anywhere in this project's userland yet, same real gap already
# documented for CFSocket.c in docs/backlog.md item d7) and
# LIBXML_ZLIB_ENABLED (xmlIO.c's real gz*() compressed-I/O path --
# this project's vendored third_party/u-boot zlib has no gzFile/gzopen/
# gzread family, confirmed absent by build_simple_cmds.sh's own real
# investigation). A real trial link confirmed both actually needed
# (undefined _bind/_socket/_getaddrinfo/etc and _gzopen/_deflate/
# _inflate/_crc32/etc without this). CFXMLInterface.c's real need (DOM
# tree parsing/validation, no XML source ever arrives over the network
# or pre-gzipped in this project's call graph) doesn't touch either.
sed -i '' -e 's/^#define LIBXML_HTTP_ENABLED$/\/* #undef LIBXML_HTTP_ENABLED -- disabled, see build_libxml2.sh *\//' \
          -e 's/^#define LIBXML_FTP_ENABLED$/\/* #undef LIBXML_FTP_ENABLED -- disabled, see build_libxml2.sh *\//' \
          -e 's/^#define LIBXML_ZLIB_ENABLED$/\/* #undef LIBXML_ZLIB_ENABLED -- disabled, see build_libxml2.sh *\//' \
          "$OUT/xml2_headers/libxml/xmlversion.h"
# xmlIO.c's own real gz*() usage is guarded by config.h's HAVE_ZLIB_H,
# a SEPARATE macro from xmlversion.h's LIBXML_ZLIB_ENABLED above (a real
# trial link confirmed _gzopen/_gzread/etc were still undefined after
# disabling only the latter) -- disable this one too, same real reason.
sed -i '' -e 's/^#define HAVE_ZLIB_H 1$/\/* #undef HAVE_ZLIB_H -- disabled, see build_libxml2.sh *\//' \
    "$OUT/xml2_headers/config.h"
# Real, narrow side effect of disabling HAVE_ZLIB_H: HTMLparser.c's own
# real `#include <zlib.h>` (now skipped) was the transitive source of
# `ptrdiff_t` for its own unrelated (non-compression) local use --
# real zlib.h includes <stddef.h> internally, ours no longer does.
# Force-including the real, standard, always-available <stddef.h>
# directly restores it -- see CFLAGS's `-include stddef.h` above.

FILES=(SAX.c entities.c encoding.c error.c parserInternals.c \
    parser.c tree.c hash.c list.c xmlIO.c xmlmemory.c uri.c \
    valid.c xlink.c HTMLparser.c HTMLtree.c debugXML.c xpath.c \
    xpointer.c xinclude.c nanohttp.c nanoftp.c DOCBparser.c \
    catalog.c globals.c threads.c c14n.c xmlstring.c buf.c \
    xmlregexp.c xmlschemas.c xmlschemastypes.c xmlunicode.c \
    xmlreader.c relaxng.c dict.c SAX2.c \
    xmlwriter.c legacy.c chvalid.c pattern.c xmlsave.c \
    xmlmodule.c schematron.c xzlib.c)

mkdir -p "$OUT/xml2_obj"
ok=0
fail=0
for f in "${FILES[@]}"; do
    base=$(basename "$f" .c)
    out="$OUT/xml2_obj/$base.o"
    if "$CLANG" "${CFLAGS[@]}" "$SRC/$f" -o "$out" 2>"$OUT/xml2_obj/$base.err"; then
        ok=$((ok + 1))
        rm -f "$OUT/xml2_obj/$base.err"
    else
        fail=$((fail + 1))
        echo "  gap: $base -- $(grep -m1 'error:' "$OUT/xml2_obj/$base.err")"
    fi
done
echo "libxml2: $ok compiled, $fail gaps (see libc_build/xml2_obj/*.err for full detail)"
if [ "$fail" -ne 0 ]; then
    exit 1
fi

# No compat/extras objects here any more (static-linking cleanup, Phase
# 1d, 2026-08-30). This archive used to carry private copies of:
#
#   - Libc/secure/{memmove,strncat,strncpy,stpncpy}_chk.c -- the real
#     _FORTIFY_SOURCE tier. All four are now owned by
#     libsystem_c.dylib (build_libsystem_c.sh's own secure/ loop; the
#     three it didn't already have were added there for exactly this).
#   - cf_compat_headers/libxml2_libc_extras.c -- hand-written labs/
#     rand_r/log10. labs and the real Apple rand family (rand.c's
#     rand_r, with its real mutable `_next` seed) now come from
#     libsystem_c.dylib; log10 from the real libsystem_m.dylib.
#   - cf_compat_headers/libm_compat.c -- hand-written fmod/pow. Real
#     libsystem_m.dylib has both, and this follows the same migration
#     build_corefoundation.sh/build_ping.sh/build_simple_cmds.sh already
#     made off their own libm_compat shims once build_libm.sh existed.
#
# Both compat .c files are deliberately left in the tree rather than
# deleted, matching the precedent those three scripts set.
rm -rf "$OUT/xml2_extras_obj"
rm -f "$OUT/libxml2.a"
ar rcs "$OUT/libxml2.a" "$OUT"/xml2_obj/*.o
echo "wrote $OUT/libxml2.a ($(ls "$OUT"/xml2_obj/*.o | wc -l | tr -d ' ') objects)"

# Real per-library dylib per this project's 2026-08-25 dylib-first
# directive. Real install_name matches the Xcode 12 iPhoneOS SDK's own
# libxml2.2.tbd (a stub describing Apple's real system dylib this
# project doesn't have -- this is our own real replacement, not a
# reference to Apple's).
#
# SIBLING DYLIBS ONLY -- no static archives (static-linking cleanup,
# Phase 1d, 2026-08-30). This link line used to end with libclosure.a,
# libmalloc.a, libplatform.a, libpthread.a, libc_gen.a, libc_string.a,
# libc_stdio.a, libc_printf.a and libc_locale_for_stdio.a, which made
# libxml2.dylib a second owner of ~42 symbols its siblings already
# provide -- most visibly the entire pthread mutex/condvar tier, which
# it re-exported wholesale. Everything it actually needs now resolves
# through real sibling dylibs:
#
#   libSystem.B.dylib   -- umbrella, re-exports libsystem_c/malloc/
#                          pthread/kernel/platform/blocks (so plain
#                          libc, the _chk tier, the rand family, labs,
#                          environ, stdio and the pthread API all bind
#                          cross-image the way real Darwin does)
#   libsystem_m.dylib   -- fmod/pow/modf/log10 (real Apple/FreeBSD Libm)
#   libdispatch.dylib   -- threads.c's real dispatch_once use
#   libdyld.dylib       -- dl* / image APIs
#   libicuuc.dylib      -- encoding.c's real ICU converter path
#
# Two former `-Wl,-alias` entries went away with the archives, and had
# to: `-Wl,-alias` only lands in the real exports trie when the alias's
# BASE symbol is local to the same ld64 invocation (docs/backlog.md item
# d10), so once libpthread.a is gone, aliasing `_pthread_cond_init` here
# would silently emit a malformed ordinal-0 re-export instead of failing.
# The plain `_pthread_cond_init` spelling the iOS SDK headers emit is now
# a real, direct export of libsystem_pthread.dylib itself -- the alias
# moved there, where its base genuinely is local.
#
# The former `-Wl,-U,_time -Wl,-U,_sched_yield` deferrals are gone too:
# both are real, defined exports now (libsystem_c.dylib's extracted
# time.o, libsystem_pthread.dylib's sched_yield trap), so they resolve
# for real at link time rather than being punted to flat-namespace
# lookup.
#
# Real bug found+fixed while writing this: without `-force_load`, ld64's
# normal archive semantics only pull `.a` members needed to satisfy an
# already-outstanding undefined-symbol reference -- since nothing else
# in this link actually calls any real libxml2 API (xmlParseFile etc),
# almost the entire archive was silently dropped, producing a "clean"
# dylib link with only ~33 incidentally-pulled pthread/libc symbols
# exported and none of libxml2's own real API (confirmed via
# `nm -g`, not assumed). `-force_load` -- the same real mechanism
# build_corefoundation.sh's own trial link already uses -- forces every
# object in the archive in regardless of reference.
mkdir -p "$OUT/system"
rm -f "$OUT/system/libxml2.dylib"
# Real, undeclared prerequisites, found 2026-09-13 via a live
# build_everything.sh run: this dylib link references 7 sibling dylibs
# with zero prerequisite check, so a real "no such file" clang failure
# was silently mislabeled "undefined symbols" by the error-detection
# grep below (which only greps for the undefined-symbol-list shape, not
# a plain file-not-found line). Confirmed no cycle back to this script
# from any of the 7 (grep).
if [ ! -f "$OUT/system/libdispatch.dylib" ]; then
    echo "error: $OUT/system/libdispatch.dylib not found -- run build_libdispatch_dylib.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libsystem_m.dylib" ]; then
    echo "error: $OUT/system/libsystem_m.dylib not found -- run build_libm_dylib.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libsystem_c.dylib" ]; then
    echo "error: $OUT/system/libsystem_c.dylib not found -- run build_libsystem_c.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libsystem_malloc.dylib" ]; then
    echo "error: $OUT/system/libsystem_malloc.dylib not found -- run build_libsystem_malloc.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libsystem_platform.dylib" ]; then
    echo "error: $OUT/system/libsystem_platform.dylib not found -- run build_libsystem_platform.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libdyld.dylib" ]; then
    echo "error: $OUT/system/libdyld.dylib not found -- run build_libdyld.sh first" >&2
    exit 1
fi
if [ ! -f "$OUT/system/libSystem.B.dylib" ]; then
    echo "error: $OUT/system/libSystem.B.dylib not found -- run build_libsystem.sh first" >&2
    exit 1
fi
    # -not_for_dyld_shared_cache: real, newly-surfaced requirement of
    # the daily-driver Xcode's newer ld (NEWLD_BINDIR above) -- it
    # refuses "-undefined dynamic_lookup"/-U on a dylib it classifies
    # as shared-cache eligible (real /usr/lib install path) unless this
    # is set, a real check Xcode 12's ld64-609 never enforced. This
    # project builds no real dyld shared cache at all, so opting every
    # real dylib out is correct, not a workaround (docs/backlog.md item
    # d9's NEWLD_BINDIR sweep, 2026-08-28).
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/libxml2.2.dylib \
    -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains \
    -o "$OUT/system/libxml2.dylib" \
    -Wl,-force_load,"$OUT/libxml2.a" \
    "icu_build/target/lib/libicuuc.dylib" \
    "$OUT/system/libdispatch.dylib" \
    "$OUT/system/libsystem_m.dylib" \
    "$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_malloc.dylib" \
    `# libsystem_platform.dylib listed EXPLICITLY (DAR-13, 2026-09-04):` \
    `# this dylib's own compiled code calls plain memcpy()/memset()` \
    `# (real Apple's own real single owner, libsystem_platform.dylib) --` \
    `# was previously missing from this link line entirely (unlike every` \
    `# other sibling dylib in this project, which already lists it` \
    `# directly). Before DAR-13, that silently worked because` \
    `# libsystem_c.dylib's own (now-removed) copy resolved it instead;` \
    `# listing the real owning dylib explicitly here avoids the exact` \
    `# transitive-reexport DAR-217 class of bug (CLAUDE.md) rather than` \
    `# relying on libSystem.B.dylib's reexport chain alone.` \
    "$OUT/system/libsystem_platform.dylib" \
    "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib" \
    2>"$OUT/libxml2_dylib_link.err" \
    && echo "wrote $OUT/system/libxml2.dylib (0 undefined symbols)" \
    || { echo "libxml2 dylib link found real undefined symbols:" >&2; grep '^  "' "$OUT/libxml2_dylib_link.err" | sed 's/,.*//' | sort -u >&2; exit 1; }
