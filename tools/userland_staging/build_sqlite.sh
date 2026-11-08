#!/bin/bash
# SQLite 3.44.0 (third_party/sqlite, the upstream amalgamation pinned by
# SHA-256 in setup_third_party.sh, unmodified) as
#   libc_build/system/libsqlite3.dylib   install name /usr/lib/libsqlite3.dylib
#                                        (Apple's own path and versions, 9.0.0)
#   libc_build/media/include/sqlite3.h, sqlite3ext.h
# for Mail2.app's message index. Same toolchain and link recipe as the media
# stack (media_common.sh). Options: thread-safe (Mail2 opens the index from a
# dispatch queue), FTS3/FTS4 for search, no extension loading (no dlopen of
# arbitrary code), no zone malloc (this libmalloc has no malloc zones for it).
set -euo pipefail
cd "$(dirname "$0")"
. ./media_common.sh
# for gen_build_order.py, which reads only build_*.sh:
# run build_libsystem.sh build_libinfo.sh build_libresolv_dylib.sh first
SRC="$PWD/$TP/sqlite"
x11_require_source "$SRC/sqlite3.c"
grep -q '#define SQLITE_VERSION  *"3.44.0"' "$SRC/sqlite3.h" ||
    { echo "error: third_party/sqlite is not SQLite 3.44.0 -- recheck this script" >&2; exit 1; }

OBJ="$MEDIA_GEN/sqlite"
rm -rf "$OBJ"; mkdir -p "$OBJ"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c "${MEDIA_CFLAGS[@]}" -O2 -Wno-everything \
    -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION=1 -DSQLITE_ENABLE_FTS3 -DSQLITE_ENABLE_FTS3_PARENTHESIS \
    -DSQLITE_ENABLE_FTS4 -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_WITHOUT_ZONEMALLOC -DSQLITE_ENABLE_LOCKING_STYLE=0 \
    -DSQLITE_DEFAULT_WAL_SYNCHRONOUS=1 \
    "$SRC/sqlite3.c" -o "$OBJ/sqlite3.o" 2> "$OBJ/sqlite3.err" ||
    { cat "$OBJ/sqlite3.err" >&2; echo "error: sqlite3.c failed to compile" >&2; exit 1; }
LIB="$MEDIA_SYS/libsqlite3.dylib"
rm -f "$LIB"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib "${MEDIA_LDFLAGS[@]}" \
    -install_name /usr/lib/libsqlite3.dylib -compatibility_version 9 -current_version 9 \
    -o "$LIB" "$OBJ/sqlite3.o" "${MEDIA_SYSLIBS[@]}" 2> "$OBJ/link.err" ||
    { grep -E '^  "|error' "$OBJ/link.err" | sed 's/, referenced from:.*//' | sort -u >&2
      echo "error: libsqlite3 link failed" >&2; exit 1; }
otool -hv "$LIB" | grep -q " DYLIB " || { echo "error: $LIB is not MH_DYLIB" >&2; exit 1; }
nm -gU "$LIB" | grep -q " _sqlite3_open_v2$" || { echo "error: $LIB does not export sqlite3_open_v2" >&2; exit 1; }
./bind_target_audit.sh "$LIB"
cp "$SRC/sqlite3.h" "$SRC/sqlite3ext.h" "$MEDIA_INC/"
echo "wrote $LIB (/usr/lib/libsqlite3.dylib)"
