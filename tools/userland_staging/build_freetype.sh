#!/bin/bash
# Builds libc_build/system/libfreetype.dylib (install name
# /usr/X11/lib/libfreetype.6.dylib) from real, unmodified FreeType 2.14.3
# (third_party/freetype, gitlab.freedesktop.org/freetype/freetype tag
# VER-2-14-3 -- the version nixpkgs nixos-26.05 rev 8f0500b9 ships).
# DAR-384, X11GS 3. Helpers: fontstack_common.sh / x11_common.sh.
#
# nixpkgs applies five post-2.14.3 security fixes from upstream master
# (TrueType SHZ heap overflow, IUP integer overflow, variation-handling
# signedness; pkgs/by-name/fr/freetype/package.nix). They are NOT applied
# here: this tree clones the release tag unmodified, and carrying them
# would mean an anurodhp/* fork. Revisit if these fonts ever come from
# untrusted input.
#
# Follows FreeType's own CMakeLists.txt, the build PureDarwin uses
# (nix/pkgs/x11/xvfb-freetype.nix):
#   sources   -- BASE_SRCS (CMakeLists.txt:420), read from the file below,
#                plus builds/unix/ftsystem.c (the `elseif (UNIX)` branch,
#                :465) and src/base/ftdebug.c (:478)
#   ftconfig.h -- builds/unix/ftconfig.h.in with HAVE_UNISTD_H and
#                HAVE_FCNTL_H turned on (:332-346; both headers exist)
#   ftoption.h -- include/freetype/config/ftoption.h with
#                FT_CONFIG_OPTION_SYSTEM_ZLIB turned on (:366-370,
#                ZLIB_FOUND: libz.dylib from build_zlib.sh). bzip2, libpng,
#                harfbuzz and brotli are not found (none is built here),
#                so those stay off, exactly as CMake leaves them.
#   flags     -- FT2_BUILD_LIBRARY (:498), C_VISIBILITY_PRESET hidden (:510)
#   version   -- builds/unix/configure.raw version_info='26:6:20', turned
#                into libtool darwin numbers: libfreetype.6, compat 27,
#                current 27.6
# Public headers are staged under $X11_INC/freetype2, FreeType's own
# install layout (INSTALL_INTERFACE include/freetype2).
set -e
cd "$(dirname "$0")"
. ./fontstack_common.sh

SRC="$TP/freetype"
x11_require_source "$SRC/CMakeLists.txt"
[ -f "$OUT/system/libz.dylib" ] || { echo "error: $OUT/system/libz.dylib not found -- run build_zlib.sh first" >&2; exit 1; }
for d in libsystem_kernel libsystem_c libsystem_pthread libsystem_malloc libsystem_platform libdyld; do
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- run build_${d}.sh first" >&2; exit 1; }
done
x11_require_sys_dylibs

vi=$(sed -n "s/^version_info='\([0-9:]*\)'.*/\1/p" "$SRC/builds/unix/configure.raw")
[ "$vi" = "26:6:20" ] || { echo "error: freetype version_info is '$vi', expected 26:6:20 -- recheck this script" >&2; exit 1; }
read -r major compat current <<< "$(fs_version_info "$vi")"

OBJ="$FS_ROOT/obj/freetype"
GEN="$FS_ROOT/gen/freetype/include"
rm -rf "$OBJ" "$GEN"
mkdir -p "$OBJ" "$GEN/freetype/config"

sed -E 's/#undef +(HAVE_UNISTD_H)/#define \1 1/; s/#undef +(HAVE_FCNTL_H)/#define \1 1/' \
    "$SRC/builds/unix/ftconfig.h.in" > "$GEN/freetype/config/ftconfig.h"
sed -E 's@/\* +(#define +FT_CONFIG_OPTION_SYSTEM_ZLIB) +\*/@\1@' \
    "$SRC/include/freetype/config/ftoption.h" > "$GEN/freetype/config/ftoption.h"
grep -q '^#define FT_CONFIG_OPTION_SYSTEM_ZLIB' "$GEN/freetype/config/ftoption.h" || {
    echo "error: FT_CONFIG_OPTION_SYSTEM_ZLIB substitution did not apply" >&2; exit 1; }
grep -q '^#define HAVE_UNISTD_H 1' "$GEN/freetype/config/ftconfig.h" || {
    echo "error: HAVE_UNISTD_H substitution did not apply" >&2; exit 1; }

SRCS=$(awk '/^set\(BASE_SRCS/{f=1; next} f && /\)/{f=0} f {print $1}' "$SRC/CMakeLists.txt")
[ "$(echo "$SRCS" | wc -l)" -gt 30 ] || { echo "error: could not read BASE_SRCS from CMakeLists.txt" >&2; exit 1; }
for f in $SRCS builds/unix/ftsystem.c src/base/ftdebug.c; do
    x11_compile "$OBJ" "$SRC/$f" -DFT2_BUILD_LIBRARY -fvisibility=hidden \
        -I "$GEN" -I "$SRC/include" -I "$GEN/freetype/config"
done
x11_compile_report freetype

# Stage headers: upstream's include/ tree, then the two generated config
# headers over the top (CMakeLists.txt:600-609 installs them the same way).
rm -rf "$X11_INC/freetype2"
mkdir -p "$X11_INC/freetype2"
cp "$SRC/include/ft2build.h" "$X11_INC/freetype2/"
cp -R "$SRC/include/freetype" "$X11_INC/freetype2/"
rm -rf "$X11_INC/freetype2/freetype/internal"
cp "$GEN/freetype/config/ftconfig.h" "$GEN/freetype/config/ftoption.h" "$X11_INC/freetype2/freetype/config/"

fs_link_dylib libfreetype.dylib "$X11_PREFIX/lib/libfreetype.$major.dylib" "$compat" "$current" "$OBJ" \
    "$OUT/system/libz.dylib"
