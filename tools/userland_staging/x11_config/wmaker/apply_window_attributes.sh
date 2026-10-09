#!/bin/sh
# Applies this port's overrides to a generated global WMWindowAttributes file: no application icons. With Window Maker's
# dock off, every application's icon would otherwise sit loose on the desktop (Naples is the dock). Idempotent.
# usage: apply_window_attributes.sh <WMWindowAttributes file>
f="$1"
[ -f "$f" ] || { echo "error: $f not found" >&2; exit 1; }
grep -q '^[[:space:]]*"\*" = {' "$f" || { echo "error: no \"*\" entry in $f" >&2; exit 1; }
grep -q 'NoAppIcon' "$f" && exit 0
sed -i.bak -e 's:^\([[:space:]]*"\*" = {\):\1NoAppIcon = Yes;:' "$f"
rm -f "$f.bak"
