#!/bin/bash
# Builds Preview.app, an NSDocument image viewer (open several images, crop,
# rotate, flip, zoom, copy and paste) (our repo anurodhp/darwin-util, folder
# Preview/; PREVIEW_SRC overrides) and installs it into libc_build/gnustep/root
# as /usr/GNUstep/System/Applications/Preview.app (/Applications/Preview.app on
# the image, stage_gnustep_mac_layout.sh).
# Pure AppKit: the formats are NSBitmapImageRep's in gnustep-gui (PNG, JPEG and
# TIFF read and written, PNM read; build_gnustep_gui.sh links libpng, libjpeg,
# libtiff), so nothing more links.
# Build, install and audit: gnustep_common.sh's gs_build_app.
cd "$(dirname "$0")"
. ./gnustep_common.sh
# Source: PREVIEW_SRC, else the darwin-util checkout beside this repo
# (../darwin-util, the same level as ../naples and ../venice, where the apps
# are developed), else the pinned third_party/darwin-util clone
# (setup_third_party.sh).
if [ -z "${PREVIEW_SRC:-}" ]; then
    if [ -f "$HERE/../../../darwin-util/Preview/GNUmakefile" ]; then PREVIEW_SRC="$HERE/../../../darwin-util/Preview"
    else PREVIEW_SRC="$HERE/../../third_party/darwin-util/Preview"; fi
fi
echo "Preview source: $PREVIEW_SRC ($(git -C "$PREVIEW_SRC" describe --tags --always --dirty 2>/dev/null || echo 'not a git checkout'))"
gs_build_app Preview "$PREVIEW_SRC"
