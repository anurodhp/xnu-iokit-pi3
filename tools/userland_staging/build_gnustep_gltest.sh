#!/bin/bash
# Builds GLTest.app (tools/userland_staging/gnustep_gltest), the AppKit
# OpenGL check: an NSOpenGLView through gnustep-back's GLX glue on Mesa's
# libGL, verified with glReadPixels (prints GSGLTEST PASS/FAIL; -frames N
# reports a frame rate). Installs as /usr/GNUstep/System/Applications/
# GLTest.app in libc_build/gnustep/root (/Applications/GLTest.app on the
# image). Needs build_mesa.sh (libGL + GL headers) and build_gnustep_back.sh
# with GLX.
cd "$(dirname "$0")"
. ./gnustep_common.sh
[ -f "$OUT/x11/include/GL/gl.h" ] && [ -f "$SYS/libGL.dylib" ] ||
    { echo "error: libGL not built -- run build_mesa.sh first" >&2; exit 1; }
GS_APP_LIBS=("$SYS/libGL.dylib")
GS_APP_CPPFLAGS="-I$OUT/x11/include"
gs_build_app GLTest "$HERE/gnustep_gltest"
