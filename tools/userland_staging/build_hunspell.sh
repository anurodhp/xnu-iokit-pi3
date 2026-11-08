#!/bin/bash
# Hunspell 1.7.6 (third_party/hunspell, github.com/hunspell/hunspell tag v1.7.6,
# unmodified; tri-licensed MPL-1.1 / GPL-2+ / LGPL-2.1+, used here under the
# MPL 1.1) as
#   libc_build/system/libhunspell.dylib   install name /usr/lib/libhunspell-1.7.0.dylib
#                                         (libtool's Darwin name for -version-info 2:2:2)
#   libc_build/media/include/hunspell/    hunspell.h (the C API) and what it includes
# for Mail2.app's spell checking (its dictionaries ship in Mail2.app). The
# spell checker LibreOffice, Firefox and Chromium use; Apple's NSSpellChecker
# server is closed and GNUstep's needs one this system does not have.
# C++ against this project's libc++, as HarfBuzz (build_harfbuzz.sh). configure
# is not run (it needs automake): the library needs no config.h, only its two
# generated headers, written here from their .in files.
set -euo pipefail
cd "$(dirname "$0")"
. ./media_common.sh
# for gen_build_order.py, which reads only build_*.sh:
# run build_libsystem.sh build_libcxx_dylib.sh first
SRC="$PWD/$TP/hunspell"
x11_require_source "$SRC/src/hunspell/hunspell.cxx"
grep -q 'AC_INIT(\[hunspell\],\[1.7.6\]' "$SRC/configure.ac" ||
    { echo "error: third_party/hunspell is not 1.7.6 -- recheck this script" >&2; exit 1; }

OBJ="$MEDIA_GEN/hunspell"
INC="$MEDIA_INC/hunspell"
rm -rf "$OBJ" "$INC"
mkdir -p "$OBJ/obj" "$OBJ/gen" "$INC"
sed -e 's/@HUNSPELL_STATIC_ONLY@/0/' -e 's/@HAVE_VISIBILITY@/1/' "$SRC/src/hunspell/hunvisapi.h.in" > "$OBJ/gen/hunvisapi.h"
sed -e 's/@HUNSPELL_VERSION_MAJOR@/1/' -e 's/@HUNSPELL_VERSION_MINOR@/7/' -e 's/@HUNSPELL_VERSION_PATCH@/6/' \
    -e 's/@VERSION@/1.7.6/' "$SRC/src/hunspell/hunversion.h.in" > "$OBJ/gen/hunversion.h"
grep -q '@' "$OBJ/gen/hunvisapi.h" "$OBJ/gen/hunversion.h" && { echo "error: unsubstituted @ in a generated header" >&2; exit 1; }

CXXFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -std=c++14 "${MEDIA_CFLAGS[@]}" "${MEDIA_CXXFLAGS[@]}"
    -O2 -fexceptions -frtti -fvisibility=hidden -DBUILDING_LIBHUNSPELL -I "$OBJ/gen" -I "$SRC/src/hunspell")
n=0
for f in affentry affixmgr csutil filemgr hashmgr hunspell hunspelltrace hunzip phonet replist suggestmgr; do
    "$TC/clang++" "${CXXFLAGS[@]}" "$SRC/src/hunspell/$f.cxx" -o "$OBJ/obj/$f.o" 2> "$OBJ/obj/$f.err" ||
        { cat "$OBJ/obj/$f.err" >&2; echo "error: $f.cxx failed to compile" >&2; exit 1; }
    [ -s "$OBJ/obj/$f.err" ] || rm -f "$OBJ/obj/$f.err"
    n=$((n+1))
done
echo "compiled $n hunspell sources"

LIB="$MEDIA_SYS/libhunspell.dylib"
rm -f "$LIB"
"$TC/clang++" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib "${MEDIA_LDFLAGS[@]}" \
    -install_name /usr/lib/libhunspell-1.7.0.dylib -compatibility_version 3 -current_version 3.2 \
    -o "$LIB" "$OBJ/obj"/*.o "${MEDIA_SYSLIBS[@]}" 2> "$OBJ/link.err" ||
    { grep -E '^  "|error' "$OBJ/link.err" | sed 's/, referenced from:.*//' | sort -u >&2
      echo "error: hunspell link failed" >&2; exit 1; }
otool -hv "$LIB" | grep -q " DYLIB " || { echo "error: $LIB is not MH_DYLIB" >&2; exit 1; }
syms="$(nm -gU "$LIB")"	# once: grep -q closing the pipe early fails nm under pipefail
for s in Hunspell_create Hunspell_spell Hunspell_suggest Hunspell_free_list Hunspell_destroy Hunspell_add; do
    grep -q " _$s$" <<< "$syms" || { echo "error: $LIB does not export $s" >&2; exit 1; }
done
cp "$SRC/src/hunspell/hunspell.h" "$SRC/src/hunspell/hunspell.hxx" "$SRC/src/hunspell/w_char.hxx" \
    "$SRC/src/hunspell/atypes.hxx" "$OBJ/gen/hunvisapi.h" "$OBJ/gen/hunversion.h" "$INC/"
./bind_target_audit.sh "$LIB"
echo "wrote $LIB (/usr/lib/libhunspell-1.7.0.dylib)"
