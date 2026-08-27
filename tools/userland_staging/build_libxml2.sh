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

# Real, small compat gaps found via a real trial link (checked via nm
# against every existing curated archive first, not assumed):
# memmove_chk/strncat_chk/strncpy_chk (Libc's own real secure/*.c
# fortified functions, already used the same way by
# build_corefoundation.sh -- reused here, not reinvented), plus labs/
# rand_r (real, tiny, zero-risk standard C functions -- see
# cf_compat_headers/libxml2_libc_extras.c's own header for why these
# two specifically get a real implementation here while _time/
# _sched_yield below are deliberately deferred instead).
LIBC="../../third_party/Libc"
mkdir -p "$OUT/xml2_extras_obj"
"$CLANG" "${CFLAGS[@]}" "$LIBC/secure/memmove_chk.c" -o "$OUT/xml2_extras_obj/memmove_chk.o"
"$CLANG" "${CFLAGS[@]}" "$LIBC/secure/strncat_chk.c" -o "$OUT/xml2_extras_obj/strncat_chk.o"
"$CLANG" "${CFLAGS[@]}" "$LIBC/secure/strncpy_chk.c" -o "$OUT/xml2_extras_obj/strncpy_chk.o"
"$CLANG" "${CFLAGS[@]}" "$LIBC/secure/stpncpy_chk.c" -o "$OUT/xml2_extras_obj/stpncpy_chk.o"
"$CLANG" "${CFLAGS[@]}" "$COMPAT/libxml2_libc_extras.c" -o "$OUT/xml2_extras_obj/libxml2_libc_extras.o"
# Real fmod/pow (cf_compat_headers/libm_compat.c, already used by
# build_corefoundation.sh for the same real "no Libm anywhere" gap) --
# reused unmodified: xpath.c's own real call sites (general-double
# fmod() for XPath's `mod` operator, pow(10.0, integer-exponent) for
# scientific-notation parsing, both confirmed via source inspection)
# match the exact cases that file's fmod/pow already handle correctly.
"$CLANG" "${CFLAGS[@]}" "$COMPAT/libm_compat.c" -o "$OUT/xml2_extras_obj/libm_compat.o"

rm -f "$OUT/libxml2.a"
ar rcs "$OUT/libxml2.a" "$OUT"/xml2_obj/*.o "$OUT"/xml2_extras_obj/*.o
echo "wrote $OUT/libxml2.a ($(ls "$OUT"/xml2_obj/*.o "$OUT"/xml2_extras_obj/*.o | wc -l | tr -d ' ') objects)"

# Real per-library dylib per this project's 2026-08-25 dylib-first
# directive. Real install_name matches the Xcode 12 iPhoneOS SDK's own
# libxml2.2.tbd (a stub describing Apple's real system dylib this
# project doesn't have -- this is our own real replacement, not a
# reference to Apple's).
#
# Real remaining gaps, deliberately deferred rather than implemented
# (checked via nm first, confirmed genuinely absent everywhere in this
# project, not a quick link-order fix): _time and _sched_yield are real
# syscall-backed functions with no wrapper anywhere in this project's
# userland yet (same category as CFRunLoop/CFSocket's own already-
# documented real-syscall gaps, docs/backlog.md item d7) -- deferred via
# the established per-symbol `-Wl,-U,<name>` mechanism (confirmed
# reliable in this environment by build_libdispatch_dylib.sh's own real
# finding: a blanket `-Wl,-undefined,dynamic_lookup` does NOT work here,
# real ld64 tries resolving against the HOST macOS's own dyld shared
# cache and hard-fails for common names instead of deferring -- `-U` per
# symbol avoids that). Real pthread condition-variable functions this
# project's libpthread.a only exports under Darwin's real $UNIX2003/
# double-underscore-internal aliasing (`_pthread_cond_init$UNIX2003`,
# `__pthread_cond_wait`) get real `-Wl,-alias` entries, same established
# idiom every other dylib in this project already uses.
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
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/libxml2.2.dylib \
    -Wl,-fixup_chains \
    -Wl,-alias,'_pthread_cond_init$UNIX2003',_pthread_cond_init \
    -Wl,-alias,__pthread_cond_wait,_pthread_cond_wait \
    -Wl,-U,_time -Wl,-U,_sched_yield \
    -o "$OUT/system/libxml2.dylib" \
    -Wl,-force_load,"$OUT/libxml2.a" \
    "icu_build/target/lib/libicuuc.dylib" \
    "$OUT/system/libdispatch.dylib" \
    "$OUT/system/libdyld.dylib" "$OUT/system/libSystem.B.dylib" \
    "$OUT/libclosure.a" "$OUT/libmalloc.a" "$OUT/libplatform.a" \
    "$OUT/libpthread.a" "$OUT/libc_gen.a" "$OUT/libc_string.a" "$OUT/libc_stdio.a" "$OUT/libc_printf.a" \
    "$OUT/libc_locale_for_stdio.a" \
    2>"$OUT/libxml2_dylib_link.err" \
    && echo "wrote $OUT/system/libxml2.dylib (0 undefined symbols beyond the deliberate -U deferrals)" \
    || { echo "libxml2 dylib link found real undefined symbols:" >&2; grep '^  "' "$OUT/libxml2_dylib_link.err" | sed 's/,.*//' | sort -u >&2; exit 1; }
