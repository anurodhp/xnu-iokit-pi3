#!/bin/bash
# Builds TextTest.app (tools/userland_staging/gnustep_texttest): the AppKit
# Unicode text check. Renders Latin, CJK, hangul, Hebrew and emoji above
# U+FFFF offscreen through gnustep-gui's glyph generator and gnustep-back's
# cairo fonts (UTF-32 glyphs + fallback faces), writes /tmp/texttest*.png and
# prints "GSTEXTTEST PASS"/"FAIL n". Installs as /usr/GNUstep/System/
# Applications/TextTest.app in libc_build/gnustep/root. Needs
# build_gnustep_gui.sh and build_gnustep_back.sh.
cd "$(dirname "$0")"
. ./gnustep_common.sh
gs_build_app TextTest "$HERE/gnustep_texttest"
