#!/bin/bash
# Builds Rome.app, a small fast GNUstep terminal (third_party/rome, our repo
# anurodhp/rome, pinned in setup_third_party.sh), and installs it into
# libc_build/gnustep/root as /usr/GNUstep/System/Applications/Rome.app
# (/Applications/Rome.app on the image, stage_gnustep_mac_layout.sh).
#
# Rome runs the login shell on a pty (forkpty, libsystem_c), emulates the
# terminal with libghostty-vt (build_libghostty_vt.sh), rasterises JetBrains Mono (with Inter,
# DejaVu, GNU Unifont and Noto Emoji behind it for glyphs it lacks)
# with fontconfig + FreeType into a glyph atlas, and draws only the cells
# that changed, into a child X window on its own X connection: by default
# with the CPU into an MIT-SHM image (Xext), or with -RomeRenderer gl through
# Mesa's libGL (the V3D on the Pi, softpipe elsewhere). The README in the
# repo has the design and the measurements behind the default.
# Build, install and audit: gnustep_common.sh's gs_build_app.
cd "$(dirname "$0")"
. ./gnustep_common.sh
# gs_build_app checks this; the line is here so gen_build_order.py sees the
# edge: run build_gnustep_host_tools.sh and build_gnustep_back.sh first.
[ -f "$SYS/libghostty-vt.dylib" ] && [ -f "$OUT/libghostty_vt_include/ghostty/vt.h" ] ||
    { echo "error: libghostty-vt not built -- run build_libghostty_vt.sh first" >&2; exit 1; }
[ -f "$OUT/x11/include/GL/gl.h" ] && [ -f "$SYS/libGL.dylib" ] ||
    { echo "error: libGL not built -- run build_mesa.sh first" >&2; exit 1; }
for l in libfontconfig libfreetype libXext libX11; do
    [ -f "$SYS/$l.dylib" ] || { echo "error: $SYS/$l.dylib not built (X11/font stack)" >&2; exit 1; }
done
nm -gU "$SYS/libsystem_c.dylib" | grep " _forkpty$" > /dev/null ||
    { echo "error: libsystem_c.dylib does not export _forkpty -- run build_libc_gen.sh and build_libsystem_c.sh first" >&2; exit 1; }
GS_APP_LIBS=("$SYS/libghostty-vt.dylib" "$SYS/libfontconfig.dylib" "$SYS/libfreetype.dylib"
    "$SYS/libGL.dylib" "$SYS/libXext.dylib" "$SYS/libX11.dylib")
GS_APP_CPPFLAGS="-I$OUT/x11/include -I$OUT/x11/include/freetype2 -I$OUT/libghostty_vt_include"
# ROME_SRC=<dir> builds another checkout of rome (development); the default
# is the pinned third_party/rome.
gs_build_app Rome "${ROME_SRC:-$HERE/../../third_party/rome}"
