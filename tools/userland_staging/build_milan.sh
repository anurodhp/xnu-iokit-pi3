#!/bin/bash
# Builds Milan.app, a small fast GNUstep file manager (Finder-style; our repo
# anurodhp/milan, pinned in setup_third_party.sh as third_party/milan, MILAN_SRC overrides) and installs it into
# libc_build/gnustep/root as /usr/GNUstep/System/Applications/Milan.app.
# Pure AppKit + GCD (directory loads and file operations run on dispatch
# queues, live refresh uses a kqueue vnode dispatch source); no extra libs.
# Build, install and audit: gnustep_common.sh's gs_build_app.
cd "$(dirname "$0")"
. ./gnustep_common.sh
GS_APP_LIBS=("$SYS/libjpeg.dylib" "$SYS/libX11.dylib" "$SYS/libxcb.dylib" "$SYS/libXau.dylib" "$SYS/libXdmcp.dylib")
GS_APP_CPPFLAGS="-I$OUT/x11/include"
gs_build_app Milan "${MILAN_SRC:-$HERE/../../third_party/milan}"
