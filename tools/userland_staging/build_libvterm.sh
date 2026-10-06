#!/bin/bash
# Builds libvterm 0.3.3 (third_party/libvterm, github.com/neovim/libvterm tag
# v0.3.3 -- the git mirror of Paul Evans' bzr upstream, lp:libvterm, that
# carries its release tags; MIT licence) as a dylib for Rome.app
# (build_rome.sh), the terminal emulator's VT220/xterm parser and screen:
#   libc_build/system/libvterm.dylib   (/usr/lib/libvterm.0.dylib)
#   libc_build/libvterm_include/       (vterm.h, vterm_keycodes.h)
# inject_into_sd_image.sh installs every libc_build/system dylib at its own
# install name, so it lands at /usr/lib/libvterm.0.dylib on the image.
#
# Upstream's Makefile builds libvterm.la with libtool, -version-info 0:0:0
# (Makefile:52-54), which is libvterm.0.dylib with compatibility/current
# version 1 on Darwin; the same names are used here. Sources: every
# src/*.c (Makefile:30), compiled with its CFLAGS (-std=c99, -Iinclude).
# The two encoding tables (src/encoding/*.inc) are generated from the .tbl
# files by upstream's tbl2inc_c.pl (Makefile:72-74), into the object
# directory so the clone stays unmodified; encoding.c includes them as
# "encoding/<name>.inc", found through -I$OBJ. src/fullwidth.inc is checked
# in upstream.
set -euo pipefail
cd "$(dirname "$0")"
OUT="libc_build"
XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
[ -x "$CLANG" ] && [ -d "$SDK" ] || { echo "error: Xcode-12.app not found at $XCODE12" >&2; exit 1; }
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
[ -x "$NEWLD_BINDIR/ld" ] || { echo "error: no usable 'ld' under $DAILY_XCODE" >&2; exit 1; }
VT="../../third_party/libvterm"
[ -f "$VT/include/vterm.h" ] || { echo "error: $VT not found -- run setup_third_party.sh first" >&2; exit 1; }
grep -q "^VERSION=0.3.3$" "$VT/Makefile" || { echo "error: $VT is not libvterm 0.3.3" >&2; exit 1; }
# libvterm calls only libc/malloc/string functions (nm -u: malloc, memmove,
# snprintf, ...), so it links their three owners and nothing else.
[ -f "$OUT/system/libsystem_c.dylib" ] || { echo "error: libsystem_c.dylib not found -- run build_libsystem_c.sh first" >&2; exit 1; }
[ -f "$OUT/system/libsystem_malloc.dylib" ] || { echo "error: libsystem_malloc.dylib not found -- run build_libsystem_malloc.sh first" >&2; exit 1; }
[ -f "$OUT/system/libsystem_platform.dylib" ] || { echo "error: libsystem_platform.dylib not found -- run build_libsystem_platform.sh first" >&2; exit 1; }
SYSLIBS=("$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_malloc.dylib"
    "$OUT/system/libsystem_platform.dylib")

OBJ="$OUT/libvterm_obj"
rm -rf "$OBJ"
mkdir -p "$OBJ/encoding" "$OUT/system"
for t in "$VT"/src/encoding/*.tbl; do
    perl -CSD "$VT/tbl2inc_c.pl" "$t" > "$OBJ/encoding/$(basename "$t" .tbl).inc"
done
CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -O2 -std=c99 -Wall
    -fno-stack-protector -D_FORTIFY_SOURCE=0 -I "$VT/include" -I "$OBJ")
for f in "$VT"/src/*.c; do
    "$CLANG" "${CFLAGS[@]}" "$f" -o "$OBJ/$(basename "$f" .c).o"
done
LIB="$OUT/system/libvterm.dylib"
rm -f "$LIB"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/libvterm.0.dylib -compatibility_version 1 -current_version 1 \
    -B"$NEWLD_BINDIR" -Wl,-fixup_chains -o "$LIB" "$OBJ"/*.o "${SYSLIBS[@]}"
otool -hv "$LIB" | grep -q " DYLIB " || { echo "error: $LIB is not MH_DYLIB" >&2; exit 1; }
for s in _vterm_new _vterm_input_write _vterm_obtain_screen _vterm_screen_get_cell \
         _vterm_keyboard_unichar _vterm_screen_set_damage_merge _vterm_screen_enable_reflow; do
    nm -gU "$LIB" | grep -q " $s$" || { echo "error: $LIB does not export $s" >&2; exit 1; }
done
./bind_target_audit.sh "$LIB"
INC="$OUT/libvterm_include"
rm -rf "$INC"; mkdir -p "$INC"
cp "$VT"/include/vterm.h "$VT"/include/vterm_keycodes.h "$INC/"
echo "wrote $LIB (/usr/lib/libvterm.0.dylib), headers in $INC"
