#!/bin/bash
# Builds Mail2.app, a small IMAP mail client for GNUstep (our repo anurodhp/mail2, pinned in setup_third_party.sh as
# third_party/mail2, MAIL2_SRC overrides -- mail2's own build.sh sets it to its checkout), and installs it into
# libc_build/gnustep/root as /usr/GNUstep/System/Applications/Mail2.app (/Applications/Mail2.app on the image,
# stage_gnustep_mac_layout.sh).
# IMAP, SMTP and MIME are libetpan's (build_libetpan.sh), over Mbed TLS (build_mbedtls.sh) through a stream driver
# of Mail2's own; the message index is SQLite (build_sqlite.sh) and the messages Maildir files; HTML mail is turned
# into text with libxml2 (build_libxml2.sh); spelling is Hunspell's (build_hunspell.sh).
# Build, install and audit: gnustep_common.sh's gs_build_app.
cd "$(dirname "$0")"
. ./gnustep_common.sh
# for gen_build_order.py, which reads only build_*.sh:
# run build_libetpan.sh build_sqlite.sh build_mbedtls.sh build_libxml2.sh build_hunspell.sh build_gnustep_back.sh first
for l in libetpan libsqlite3 libmbedtls libmbedx509 libmbedcrypto libxml2 libiconv libhunspell; do
    [ -f "$SYS/$l.dylib" ] || { echo "error: $SYS/$l.dylib not built (see the line above)" >&2; exit 1; }
done
[ -f "$OUT/media/include/libetpan/mailimap.h" ] || { echo "error: libetpan headers not staged -- run build_libetpan.sh" >&2; exit 1; }
GS_APP_LIBS=("$SYS/libetpan.dylib" "$SYS/libsqlite3.dylib" "$SYS/libmbedtls.dylib" "$SYS/libmbedx509.dylib"
    "$SYS/libmbedcrypto.dylib" "$SYS/libxml2.dylib" "$SYS/libhunspell.dylib")
GS_APP_CPPFLAGS="-I$OUT/media/include -I$OUT/xml2_headers"
# make does not notice a changed resource list in the kept work copy: start the bundle afresh
rm -rf "$G/mail2-src/Mail2.app"
MAIL2_SRC="${MAIL2_SRC:-$HERE/../../third_party/mail2}"
echo "Mail2 source: $MAIL2_SRC ($(git -C "$MAIL2_SRC" describe --tags --always --dirty 2>/dev/null || echo 'not a git checkout'))"
gs_build_app Mail2 "$MAIL2_SRC"
