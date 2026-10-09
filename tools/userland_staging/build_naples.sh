#!/bin/bash
# Builds Naples.app, the dock and global menu bar for GNUstep + Window Maker (our
# repo anurodhp/naples, pinned in setup_third_party.sh as third_party/naples,
# NAPLES_SRC overrides) and installs it into libc_build/gnustep/root as
# /usr/GNUstep/System/Applications/Naples.app (/Applications/Naples.app on the
# image, stage_gnustep_mac_layout.sh).
# Pure AppKit + Xlib + libdispatch: it talks to Window Maker over EWMH on its
# own X connection (libX11, libXext for the XShape corners) and serves the Eau
# theme's menu protocol over Distributed Objects.
# Build, install and audit: gnustep_common.sh's gs_build_app.
cd "$(dirname "$0")"
. ./gnustep_common.sh
for l in libX11 libXext libxcb libXau libXdmcp; do
    [ -f "$SYS/$l.dylib" ] || { echo "error: $SYS/$l.dylib not built (X11 stack)" >&2; exit 1; }
done
GS_APP_LIBS=("$SYS/libX11.dylib" "$SYS/libXext.dylib" "$SYS/libxcb.dylib" "$SYS/libXau.dylib" "$SYS/libXdmcp.dylib")
GS_APP_CPPFLAGS="-I$OUT/x11/include"
gs_build_app Naples "${NAPLES_SRC:-$HERE/../../third_party/naples}"
