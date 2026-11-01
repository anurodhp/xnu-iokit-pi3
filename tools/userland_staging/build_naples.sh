#!/bin/bash
# Builds Naples.app, the dock and global menu bar for GNUstep + Window Maker (our
# repo anurodhp/naples) and installs it into libc_build/gnustep/root as
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
# The Wi-Fi panel talks to BCM43430Wifi through its user-client ABI header
# (NaplesWifi.m); IOKitLib itself is opened with dlopen, so nothing more links.
GS_APP_CPPFLAGS="-I$OUT/x11/include -I$HERE/../../iokit/BCM43430Wifi"
# make does not notice a changed VERSION, application icon or resource list in the kept work copy: start the bundle afresh
rm -rf "$G/naples-src/Naples.app"
# Source: NAPLES_SRC, else the naples checkout beside this repo (../naples, the
# same level as ../venice, where Naples is developed), else the pinned
# third_party/naples clone from setup_third_party.sh.
if [ -z "${NAPLES_SRC:-}" ]; then
    if [ -f "$HERE/../../../naples/GNUmakefile" ]; then NAPLES_SRC="$HERE/../../../naples"
    else NAPLES_SRC="$HERE/../../third_party/naples"; fi
fi
echo "Naples source: $NAPLES_SRC ($(git -C "$NAPLES_SRC" describe --tags --always --dirty 2>/dev/null || echo 'not a git checkout'))"
gs_build_app Naples "$NAPLES_SRC"
