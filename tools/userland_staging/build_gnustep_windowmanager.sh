#!/bin/bash
# Builds Gershwin's WindowManager.app (Gershwin M2, docs/gershwin-feasibility.md)
# from third_party/gershwin-windowmanager (our fork anurodhp/gershwin-windowmanager,
# branch iokit-port, off gershwin-desktop/gershwin-windowmanager 10b6797): a
# reparenting X11 window manager, Objective-C on GNUstep over raw XCB, with the
# titlebars drawn by the active GSTheme (Eau). Installed into
# libc_build/gnustep/root as /usr/GNUstep/System/Library/CoreServices/
# Applications/WindowManager.app (the build layout; on the image it is
# /System/Library/CoreServices/Applications/WindowManager.app, where Gershwin
# puts it, see stage_gnustep_mac_layout.sh).
#
# The fork's GNUmakefile takes IOKIT_PORT=1: -O2 and no pkg-config (the cross
# build has none; xcb_aux is part of libxcb-util). Compositing is off unless
# the WM is started with -ec (main.m). Link: the xcb/X11 dylibs first as owners
# (build_libxcb.sh, build_libxcb_util.sh), then gs_build_app's usual stack.
cd "$(dirname "$0")"
. ./gnustep_common.sh
for l in libxcb libxcb-icccm libxcb-xfixes libxcb-cursor libxcb-shape libxcb-randr libxcb-util \
         libxcb-composite libxcb-render libxcb-damage libxcb-shm libxcb-present libxcb-image \
         libxcb-render-util libxcb-sync libXext libX11 libXau libXdmcp; do
    [ -f "$SYS/$l.dylib" ] || { echo "error: $SYS/$l.dylib not found -- run build_libxcb.sh / build_libxcb_util.sh / build_libX11.sh first" >&2; exit 1; }
done
# WM_PROFILE=1 builds the fork's CPU probes (SIGUSR1 / every 10 s to stderr).
# Owners first: bind_target_audit.sh needs each bind recorded against the
# dylib that really exports the symbol, and ld records the first listed one.
GS_APP_LIBS=("$SYS/libxcb-cursor.dylib" "$SYS/libxcb-image.dylib" "$SYS/libxcb-render-util.dylib"
    "$SYS/libxcb-icccm.dylib" "$SYS/libxcb-util.dylib" "$SYS/libxcb-xfixes.dylib" "$SYS/libxcb-shape.dylib"
    "$SYS/libxcb-randr.dylib" "$SYS/libxcb-composite.dylib" "$SYS/libxcb-damage.dylib"
    "$SYS/libxcb-present.dylib" "$SYS/libxcb-sync.dylib" "$SYS/libxcb-render.dylib" "$SYS/libxcb-shm.dylib"
    "$SYS/libXext.dylib" "$SYS/libX11.dylib" "$SYS/libxcb.dylib" "$SYS/libXau.dylib" "$SYS/libXdmcp.dylib")
GS_APP_CPPFLAGS="-I$OUT/x11/include"
GS_APP_DIR=Library/CoreServices/Applications
# An explicit <app>_INSTALL_DIR is not prefixed with DESTDIR by gnustep-make
# (Makefiles/common.make:353-361 prefixes only the GNUSTEP_*_APPS defaults),
# so the fork's hard-coded /System/Library/CoreServices/Applications is
# overridden with the path under root/ itself.
GS_APP_MAKE=(IOKIT_PORT=1 ${WM_PROFILE:+PROFILE=1} "WindowManager_INSTALL_DIR=$ROOT$PREFIX/System/Library/CoreServices/Applications")
gs_build_app WindowManager "$HERE/../../third_party/gershwin-windowmanager/WindowManager"
