#!/bin/bash
# Builds Turin.app, System Preferences for GNUstep + Window Maker (our repo
# anurodhp/turin, pinned in setup_third_party.sh as third_party/turin,
# TURIN_SRC overrides) and installs it into libc_build/gnustep/root as
# /usr/GNUstep/System/Applications/Turin.app (/Applications/Turin.app on the
# image, stage_gnustep_mac_layout.sh).
# Pure AppKit + libdispatch. Its panes: Wi-Fi (BCM43430Wifi's user client, as
# Naples' Wi-Fi panel), Network (getifaddrs, ipconfig), Date & Time
# (settimeofday, the zoneinfo database inject_into_sd_image.sh installs),
# Background (Milan's wallpaper defaults; JPEG thumbnails through libjpeg, as
# Milan decodes them) and Users (pw(8), build_pw.sh).
# Build, install and audit: gnustep_common.sh's gs_build_app.
cd "$(dirname "$0")"
. ./gnustep_common.sh
# gs_build_app checks this; the line is here so gen_build_order.py sees the
# edge (as in build_rome.sh): run build_gnustep_host_tools.sh and build_gnustep_back.sh first.
[ -f "$SYS/libjpeg.dylib" ] || { echo "error: $SYS/libjpeg.dylib not built -- run build_libjpeg.sh first" >&2; exit 1; }
GS_APP_LIBS=("$SYS/libjpeg.dylib")
# The Wi-Fi pane talks to BCM43430Wifi through its user-client ABI header
# (TurinWifi.m); IOKitLib itself is opened with dlopen, so nothing more links.
GS_APP_CPPFLAGS="-I$OUT/x11/include -I$HERE/../../iokit/BCM43430Wifi"
# make does not notice a changed VERSION, application icon or resource list in the kept work copy: start the bundle afresh
rm -rf "$G/turin-src/Turin.app"
gs_build_app Turin "${TURIN_SRC:-$HERE/../../third_party/turin}"
