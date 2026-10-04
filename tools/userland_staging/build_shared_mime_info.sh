#!/bin/bash
# Builds the freedesktop shared-mime-info database for the image: libc_build/shared_mime_info/root/usr/share/mime
# (mime.cache and the XML it was generated from), installed by inject_into_sd_image.sh.
#
# Why: GIO (GLib, and WebKit on top of it) decides the content type of a file from /usr/share/mime/mime.cache. Without it every
# file is application/octet-stream: WebKit loaded file:///.../page.html and displayed nothing (it is not an HTML type).
#
# Source: third_party/shared-mime-info-2.4 (setup_third_party.sh, SHA-256 checked). update-mime-database is a build-host
# program, so the project is built natively with the host's glib and libxml2 (Homebrew: `brew install glib libxml2`) into a
# scratch prefix and only the generated data is staged; the data is architecture independent (mime.cache is big-endian
# whatever the CPU), so nothing here is cross-compiled and the Pi runs no code from this package.
set -euo pipefail
cd "$(dirname "$0")"
SRC="$(cd ../../third_party/shared-mime-info-2.4 2>/dev/null && pwd)" || { echo "error: third_party/shared-mime-info-2.4 not found -- run setup_third_party.sh" >&2; exit 1; }
command -v meson >/dev/null && command -v ninja >/dev/null || { echo "error: meson and ninja are needed on the host (brew install meson ninja)" >&2; exit 1; }
pkg-config --exists glib-2.0 libxml-2.0 || { echo "error: host glib-2.0 and libxml-2.0 are needed (brew install glib libxml2)" >&2; exit 1; }
WORK="$(pwd)/libc_build/shared_mime_info"
rm -rf "$WORK"
mkdir -p "$WORK"
meson setup "$WORK/build" "$SRC" --prefix "$WORK/host" -Dupdate-mimedb=true -Dbuild-tools=true -Dbuild-translations=false -Dbuild-tests=false > "$WORK/configure.log"
ninja -C "$WORK/build" install > "$WORK/build.log"
OUT="$WORK/root/usr/share/mime"
mkdir -p "$(dirname "$OUT")"
cp -R "$WORK/host/share/mime" "$OUT"
for f in mime.cache globs2 magic packages/freedesktop.org.xml; do
    [ -f "$OUT/$f" ] || { echo "error: $OUT/$f was not generated" >&2; exit 1; }
done
grep -q "text/html" "$OUT/globs2" || { echo "error: no text/html rule in the generated globs2" >&2; exit 1; }
echo "built MIME database: $(du -sk "$OUT" | cut -f1) KB in $OUT"
