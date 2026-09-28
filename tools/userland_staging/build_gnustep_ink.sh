#!/bin/bash
# Builds Ink.app (DAR-390/DAR-394), GNUstep's small document-based text
# editor (NSDocument, NSTextView, RTF/plain text), from
# third_party/gnustep-examples (gnustep/tests-examples, gui/Ink), and
# installs it into libc_build/gnustep/root as
# /usr/GNUstep/System/Applications/Ink.app. It is the first AppKit app on
# this port: run it on the target with `openapp Ink` (or
# /usr/GNUstep/System/Applications/Ink.app/Ink) under an X server.
#
# Link recipe: gnustep_common.sh, like gui and back. The app itself links
# only gnustep-gui and gnustep-base; the owners are listed so the
# executable's binds name them.
cd "$(dirname "$0")"
. ./gnustep_common.sh
INK_SRC="$HERE/../../third_party/gnustep-examples/gui/Ink"
[ -f "$INK_SRC/GNUmakefile" ] || { echo "error: $INK_SRC not found -- run setup_third_party.sh" >&2; exit 1; }
[ -d "$ROOT$PREFIX/System/Library/Bundles/libgnustep-back-032.bundle" ] ||
    { echo "error: gnustep-back not installed -- run build_gnustep_back.sh first" >&2; exit 1; }
gs_setup_link "${GS_BASE_SYSLIBS[@]}"
gs_build_env
APP_CPPFLAGS="${TARGET[*]} -D_FORTIFY_SOURCE=0 -I$GS_SYS_HEADERS -I$OUT/objc4_public_headers"

rsync -a --delete --exclude .git "$INK_SRC/" "$G/ink-src/"
cd "$G/ink-src"
make DYLIB_EXTRA_FLAGS= GNUSTEP_INSTALLATION_DOMAIN=SYSTEM CPPFLAGS="$APP_CPPFLAGS" \
    LDFLAGS="${TARGET[*]} $LINK $LIBFLAGS" > make.out 2>&1 ||
    { grep -E "error:|Undefined|^ld: |^  \"" make.out | grep -v "ld: warning" | head -30 || true
      echo "error: Ink build failed, see $G/ink-src/make.out" >&2; exit 1; }
rm -rf "$ROOT$PREFIX/System/Applications/Ink.app"
make install DESTDIR="$ROOT" GNUSTEP_INSTALLATION_DOMAIN=SYSTEM > install.out 2>&1 ||
    { tail -20 install.out; echo "error: Ink install failed" >&2; exit 1; }
APP="$ROOT$PREFIX/System/Applications/Ink.app"
[ -x "$APP/Ink" ] || { echo "error: $APP/Ink not installed" >&2; exit 1; }
otool -l "$APP/Ink" | grep -q "cmd LC_MAIN" || { echo "error: $APP/Ink has no LC_MAIN" >&2; exit 1; }
gs_audit_tree "$APP"
echo "installed $APP"
