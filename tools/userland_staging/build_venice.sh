#!/bin/bash
# Builds Venice.app, a small GNUstep video player on libmpv (our repo
# anurodhp/venice; VENICE_SRC=<dir> builds a checkout, the default is
# third_party/venice), and installs it into libc_build/gnustep/root as
# /usr/GNUstep/System/Applications/Venice.app (/Applications/Venice.app on
# the image, stage_gnustep_mac_layout.sh).
#
# Venice drives libmpv (build_libmpv.sh) through its render API into a
# child X window of its GNUstep window, on its own X connection: with
# OpenGL through Mesa's libGL by default (the V3D on the Pi with MESA_VC4=1,
# softpipe elsewhere), or with mpv's software renderer into an MIT-SHM
# image (-VeniceRenderer sw). Audio is libmpv's audiohal output to audiod.
# Build, install and audit: gnustep_common.sh's gs_build_app.
cd "$(dirname "$0")"
. ./gnustep_common.sh
[ -f "$SYS/libmpv.dylib" ] && [ -f "$OUT/media/include/mpv/render_gl.h" ] ||
    { echo "error: libmpv not built -- run build_libmpv.sh first" >&2; exit 1; }
[ -f "$OUT/x11/include/GL/gl.h" ] && [ -f "$SYS/libGL.dylib" ] ||
    { echo "error: libGL not built -- run build_mesa.sh first" >&2; exit 1; }
for l in libXext libX11; do
    [ -f "$SYS/$l.dylib" ] || { echo "error: $SYS/$l.dylib not built (X11 stack)" >&2; exit 1; }
done
GS_APP_LIBS=("$SYS/libmpv.dylib" "$SYS/libGL.dylib" "$SYS/libXext.dylib" "$SYS/libX11.dylib")
GS_APP_CPPFLAGS="-I$OUT/x11/include -I$OUT/media/include"
# make does not notice a changed resource list in the kept work copy: start the bundle afresh
rm -rf "$G/venice-src/Venice.app"
gs_build_app Venice "${VENICE_SRC:-$HERE/../../third_party/venice}"
