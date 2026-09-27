#!/bin/bash
# Stages the XKB keymap database from real, unmodified xkeyboard-config-2.47
# (third_party/xkeyboard-config) into libc_build/x11/server/root/usr/X11/
# share/, which inject_into_sd_image.sh copies onto the image. DAR-387,
# X11GS 4: xkbcomp compiles the X server's keymaps from it.
#
# This is architecture-independent data, so it is built the way upstream
# builds it: its own meson.build, run on the host with an install into a
# staging DESTDIR. That runs upstream's rules generators
# (rules/merge.py, generate-options-symbols.py, compat/map-variants.py;
# rules/meson.build) to produce the base and evdev rules files. There is
# nothing to cross-compile.
# Options: prefix /usr/X11 (this port's X11 prefix, x11_common.sh), so the
# data lands in /usr/X11/share/xkeyboard-config-2 with upstream's legacy
# /usr/X11/share/X11/xkb symlink to it (meson.build:124-130), the directory
# xkbcomp (build_xkbcomp.sh) and the server (build_xserver.sh) use.
# nls=false: translations of the rule descriptions only (po/); nothing
# here reads them. compat-rules keeps its default (true).
# Needs meson >= 0.61 + ninja + python3 >= 3.11, or >= 3.9 with the strenum
# module, on the host (meson.build:4, rules/meson.build:153-164).
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

SRC="$TP/xkeyboard-config"
x11_require_source "$SRC/meson.build"
grep -q "version: '2.47'" "$SRC/meson.build" || { echo "error: xkeyboard-config is not 2.47 -- recheck this script" >&2; exit 1; }
command -v meson >/dev/null || { echo "error: meson not found on the host" >&2; exit 1; }

BUILD="$X11_ROOT/gen/xkeyboard-config-build"
DEST="$PWD/$X11_ROOT/server/root"
rm -rf "$BUILD" "$DEST/usr/X11/share/xkeyboard-config-2" "$DEST/usr/X11/share/X11/xkb"
meson setup "$BUILD" "$SRC" --prefix=/usr/X11 -Dnls=false >/dev/null
meson install -C "$BUILD" --destdir "$DEST" >/dev/null
for f in rules/evdev rules/base keycodes/evdev symbols/us types/complete compat/complete; do
    [ -s "$DEST/usr/X11/share/xkeyboard-config-2/$f" ] || { echo "error: $f missing after meson install" >&2; exit 1; }
done
[ "$(readlink "$DEST/usr/X11/share/X11/xkb")" = /usr/X11/share/xkeyboard-config-2 ] || { echo "error: /usr/X11/share/X11/xkb symlink not installed" >&2; exit 1; }
echo "staged xkeyboard-config 2.47 in $DEST/usr/X11/share ($(du -sh "$DEST/usr/X11/share/xkeyboard-config-2" | cut -f1))"
