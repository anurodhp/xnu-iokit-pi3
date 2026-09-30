#!/bin/bash
# Builds libc_build/system/libpanel.dylib -- real, unmodified ncurses panel
# library (third_party/ncurses/ncurses/panel/*.c, same pin as libncurses).
# Real Apple top (build_top.sh) links -lpanel. Real per-library dylib, not a
# static archive: links against the real libncurses.dylib + libsystem_*.
set -euo pipefail
cd "$(dirname "$0")"
OUT="libc_build"
T="../../third_party"
P="$T/ncurses/ncurses/panel"
XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
NEWLD_BINDIR="$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain/usr/bin"
S="$OUT/system"
for f in libncurses libsystem_c libsystem_malloc libsystem_platform libsystem_kernel; do
    [ -f "$S/$f.dylib" ] || { echo "error: $S/$f.dylib not found" >&2; exit 1; }
done
[ -d "$OUT/ncurses_derived" ] || { echo "error: run build_ncurses.sh first" >&2; exit 1; }
OBJ="$OUT/libpanel_obj"
rm -rf "$OBJ"; mkdir -p "$OBJ"
CFLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -O0 -Wno-everything -D_FORTIFY_SOURCE=0 \
  -DHAVE_CONFIG_H -DNCURSES_INTERNALS -U_XOPEN_SOURCE -D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENDED -DNDEBUG -DSIGWINCH=28 \
  -I $P -I $OUT/ncurses_derived -I $T/ncurses/ncurses/ncurses -I $T/ncurses/ncurses/include"
for f in "$P"/*.c; do
    "$CLANG" $CFLAGS "$f" -o "$OBJ/$(basename "$f" .c).o"
done
rm -f "$S/libpanel.dylib"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/libpanel.5.4.dylib -Wl,-not_for_dyld_shared_cache \
    -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o "$S/libpanel.dylib" "$OBJ"/*.o \
    "$S/libncurses.dylib" "$S/libsystem_c.dylib" "$S/libsystem_malloc.dylib" \
    "$S/libsystem_platform.dylib" "$S/libsystem_kernel.dylib"
otool -hv "$S/libpanel.dylib" | grep -q " DYLIB " || { echo "error: not a DYLIB" >&2; exit 1; }
echo "wrote $S/libpanel.dylib"
