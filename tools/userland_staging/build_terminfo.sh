#!/bin/bash
# Builds a small terminfo database for the image (DAR-293): top, vim, libedit
# and the shell tools (ncurses, built without a database, build_ncurses.sh)
# otherwise fail with "error opening terminal: linux" (Terminal.app exports
# TERM=linux, gnustep-gap system-apps/Terminal/TerminalView.m:2352; the serial
# console and ssh give linux/xterm).
#
# Source: the real ncurses misc/terminfo.src (third_party/ncurses, Apple's
# ncurses tree), compiled on the host with `tic` into the layout the target's
# libncurses reads at runtime (USE_DATABASE=1: /usr/share/terminfo/<c>/<name>,
# read_entry.c in the same tree). Output: libc_build/terminfo/root/usr/share/terminfo,
# installed by inject_into_sd_image.sh.
set -euo pipefail
cd "$(dirname "$0")"
SRC="../../third_party/ncurses/ncurses/misc/terminfo.src"
[ -f "$SRC" ] || { echo "error: $SRC not found -- run setup_third_party.sh" >&2; exit 1; }
OUT="libc_build/terminfo/root/usr/share/terminfo"
NAMES="linux,vt100,vt102,vt220,ansi,dumb,xterm,xterm-color,xterm-256color,xterm-xfree86,xterm-basic,screen,screen-256color,tmux,tmux-256color,cons25,rxvt,sun"
rm -rf "libc_build/terminfo"
mkdir -p "$OUT"
tic -x -e "$NAMES" -o "$OUT" "$SRC"
for t in linux vt100 xterm xterm-256color dumb; do
    f=$(find "$OUT" -type f -name "$t" | head -1)
    [ -n "$f" ] || { echo "error: terminfo entry $t was not compiled" >&2; exit 1; }
done
echo "built terminfo: $(find "$OUT" -type f | wc -l | tr -d ' ') entries, $(du -sk "$OUT" | cut -f1) KB, in $OUT"
find "$OUT" -type f | sed "s#$OUT/##" | sort | tr '\n' ' '; echo
