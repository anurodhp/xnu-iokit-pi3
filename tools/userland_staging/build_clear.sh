#!/bin/bash
# Builds clear.macho: Apple's own `clear` (third_party/ncurses/ncurses/progs/clear.c,
# ncurses 5.7 as shipped in Apple's `ncurses` package), as a dynamic executable
# linked against this project's libncurses.dylib (build_ncurses_dylib.sh). It
# does what clear(1) does: setupterm() from $TERM, then tputs(clear_screen).
#
# The target had no clear(1): "clear: command not found" in any shell, which
# also broke scripts that use it. It needs the terminfo database on the image
# (/usr/share/terminfo, which has xterm-256color, the TERM Rome sets) and a TERM
# with a clear capability: over ssh $TERM is often "dumb", which has none, and
# clear then exits 1 with nothing printed, as on any system.
#
# inject_into_sd_image.sh installs it as /usr/bin/clear.
#
# Prerequisites: build_ncurses.sh (which stages the generated headers
# in libc_build/ncurses_derived) and build_ncurses_dylib.sh, plus the libsystem
# dylib tier; the checks below name what is missing.
set -euo pipefail
cd "$(dirname "$0")"
OUT="libc_build"
XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
[ -x "$CLANG" ] && [ -d "$SDK" ] || { echo "error: Xcode-12.app not found at $XCODE12" >&2; exit 1; }
NEWLD_BINDIR="$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain/usr/bin"
[ -x "$NEWLD_BINDIR/ld" ] || { echo "error: no usable 'ld' under $(xcode-select -p)" >&2; exit 1; }
NC="../../third_party/ncurses/ncurses"
DERIVED="$OUT/ncurses_derived"
[ -f "$NC/progs/clear.c" ] || { echo "error: $NC/progs/clear.c not found -- run setup_third_party.sh" >&2; exit 1; }
[ -f "$DERIVED/curses.h" ] && [ -f "$DERIVED/term.h" ] || { echo "error: $DERIVED/curses.h or term.h missing -- run build_ncurses.sh first" >&2; exit 1; }
for d in libncurses libsystem_c libsystem_pthread libsystem_malloc libsystem_platform libdyld libSystem.B; do
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- build it first (libncurses: build_ncurses_dylib.sh)" >&2; exit 1; }
done

OBJ="$OUT/clear_obj"
rm -rf "$OBJ"; mkdir -p "$OBJ"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -O2 -fno-stack-protector -D_FORTIFY_SOURCE=0 \
    -Wno-nullability-completeness -Wno-deprecated-declarations \
    -DHAVE_CONFIG_H -U_XOPEN_SOURCE -D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENDED -DNDEBUG \
    -I "$DERIVED" -I "$NC/progs" -I "$NC/include" -I "$NC/ncurses" \
    "$NC/progs/clear.c" -o "$OBJ/clear.o"

LINK_DYLIBS="$OUT/system/libncurses.dylib $OUT/system/libsystem_c.dylib $OUT/system/libsystem_pthread.dylib $OUT/system/libsystem_malloc.dylib $OUT/system/libsystem_platform.dylib $OUT/system/libdyld.dylib $OUT/system/libSystem.B.dylib"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o clear.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OBJ/clear.o" $LINK_DYLIBS 2>&1 | grep -v "^ld: warning" || true
[ -f clear.macho ] || { echo "error: linking clear.macho failed" >&2; exit 1; }
echo "wrote clear.macho"

bad=0
otool -l clear.macho | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || {
    echo "error: no LC_LOAD_DYLINKER /usr/lib/dyld -- not a real dynamic executable" >&2; bad=1; }
otool -l clear.macho | grep -q "cmd LC_LOAD_DYLIB" || { echo "error: no LC_LOAD_DYLIB" >&2; bad=1; }
otool -l clear.macho | grep -q "cmd LC_MAIN" || { echo "error: no LC_MAIN" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1
./bind_target_audit.sh clear.macho $LINK_DYLIBS
