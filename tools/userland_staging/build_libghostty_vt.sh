#!/bin/bash
# Builds libghostty-vt (third_party/ghostty, github.com/ghostty-org/ghostty,
# MIT licence) as a dylib for Rome.app (build_rome.sh), the terminal
# emulator's VT parser, screen, scrollback, selection and key/mouse/paste
# encoders:
#   libc_build/system/libghostty-vt.dylib   (/usr/lib/libghostty-vt.0.dylib)
#   libc_build/libghostty_vt_include/       (ghostty/vt.h and its headers)
# inject_into_sd_image.sh installs every libc_build/system dylib at its own
# install name, so it lands at /usr/lib/libghostty-vt.0.dylib on the image.
#
# The library is Zig. Zig builds the static archive for aarch64-ios (the
# object code carries no libc of its own: it binds Apple's libSystem symbols),
# and this script links the archive into a dylib the way build_libvterm.sh does:
# clang -nostdlib against this project's own libsystem dylibs, so a symbol this
# libSystem lacks fails the link or bind_target_audit.sh here rather than
# dyld on the Pi. Six symbols zig's output binds are not in this port's
# libSystem (libghostty_vt_shim.c has them: close$NOCANCEL, preadv,
# shm_unlink, and zig's compiler_rt platform checks).
#
# -Dsimd=false: the SIMD fast paths are C++ (simdutf, highway) and would need
# libc++ and a second toolchain; the scalar paths are the same code.
#
# Needs Zig 0.16.x (ZIG=<path>, else zig on PATH, else Homebrew's zig@0.16) and
# network on the first run (zig fetches the library's dependencies).
# GHOSTTY_SRC=<dir> builds another checkout (default third_party/ghostty).
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
GH="${GHOSTTY_SRC:-../../third_party/ghostty}"
[ -f "$GH/include/ghostty/vt/render.h" ] || { echo "error: $GH is not a ghostty checkout -- run setup_third_party.sh first" >&2; exit 1; }
ZIG=""
for z in "${ZIG_BIN:-}" "$(command -v zig || true)" "$(brew --prefix zig@0.16 2>/dev/null || true)/bin/zig"; do
    [ -n "$z" ] && [ -x "$z" ] && "$z" version 2>/dev/null | grep -q '^0\.16\.' && { ZIG="$z"; break; }
done
[ -n "$ZIG" ] || { echo "error: Zig 0.16.x not found (brew install zig@0.16, or ZIG_BIN=<path>)" >&2; exit 1; }
for l in libsystem_c libsystem_malloc libsystem_platform libsystem_kernel libsystem_m libsystem_pthread libdyld; do
    [ -f "$OUT/system/$l.dylib" ] || { echo "error: $OUT/system/$l.dylib not found -- run build_$l.sh first" >&2; exit 1; }
done
SYSLIBS=("$OUT/system/libsystem_c.dylib" "$OUT/system/libsystem_malloc.dylib"
    "$OUT/system/libsystem_platform.dylib" "$OUT/system/libsystem_kernel.dylib"
    "$OUT/system/libsystem_m.dylib" "$OUT/system/libsystem_pthread.dylib" "$OUT/system/libdyld.dylib")

OBJ="$OUT/libghostty_vt_obj"
rm -rf "$OBJ"
mkdir -p "$OBJ" "$OUT/system"
(cd "$GH" && "$ZIG" build -Dtarget=aarch64-ios -Dsimd=false -Doptimize=ReleaseFast -Demit-lib-vt=true \
    --prefix "$PWD/../../tools/userland_staging/$OBJ/zig" > "$PWD/../../tools/userland_staging/$OBJ/zig-build.out" 2>&1) ||
    { tail -20 "$OBJ/zig-build.out"; echo "error: zig build failed, see $OBJ/zig-build.out" >&2; exit 1; }
ARCHIVE="$OBJ/zig/lib/libghostty-vt.a"
[ -f "$ARCHIVE" ] || { echo "error: zig did not produce $ARCHIVE" >&2; exit 1; }
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -O2 -fno-stack-protector -D_FORTIFY_SOURCE=0 \
    libghostty_vt_shim.c -o "$OBJ/shim.o"
LIB="$OUT/system/libghostty-vt.dylib"
rm -f "$LIB"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/libghostty-vt.0.dylib -compatibility_version 1 -current_version 1 \
    -B"$NEWLD_BINDIR" -Wl,-fixup_chains -Wl,-all_load -o "$LIB" "$ARCHIVE" "$OBJ/shim.o" "${SYSLIBS[@]}" 2>&1 |
    grep -v "^ld: warning" || true
[ -f "$LIB" ] || { echo "error: linking $LIB failed" >&2; exit 1; }
otool -hv "$LIB" | grep -q " DYLIB " || { echo "error: $LIB is not MH_DYLIB" >&2; exit 1; }
EXPORTS="$(nm -gU "$LIB")"     # not a pipe into grep -q: SIGPIPE + pipefail
for s in _ghostty_terminal_new _ghostty_terminal_vt_write _ghostty_render_state_update _ghostty_key_encoder_encode \
         _ghostty_mouse_encoder_encode _ghostty_paste_encode _ghostty_terminal_select_all; do
    grep -q " $s\$" <<< "$EXPORTS" || { echo "error: $LIB does not export $s" >&2; exit 1; }
done
./bind_target_audit.sh "$LIB"
INC="$OUT/libghostty_vt_include"
rm -rf "$INC"; mkdir -p "$INC"
cp -R "$OBJ/zig/include/ghostty" "$INC/"
echo "wrote $LIB (/usr/lib/libghostty-vt.0.dylib), headers in $INC"
