#!/bin/bash
# Builds the Eau theme (DAR-461), a Mac-like GNUstep theme, from
# third_party/gnustep-eau-theme (our fork anurodhp/gershwin-eau-theme,
# branch iokit-port, off gershwin-desktop/gershwin-eau-theme main, LGPL-2.1;
# the original Savannah plugins/themes/Eau is not on GitHub and savannah's
# viewvc/svn is not reachable by script), and installs it into
# libc_build/gnustep/root as /usr/GNUstep/System/Library/Themes/Eau.theme.
# Select it with GSTheme = "Eau"; in /etc/GNUstep/GlobalDefaults.plist
# (tools/userland_staging/x11_config/gnustep/GlobalDefaults.plist).
#
# Builds ONLY the bundle: it links the already-installed libgnustep-gui,
# libgnustep-base and the X11 libraries, and rebuilds none of them. The
# theme is an NSBundle that gnustep-gui dlopen()s, so bind_target_audit.sh
# is run on the bundle's executable (gs_audit_tree).
# run build_gnustep_host_tools.sh and build_gnustep_back.sh first.
cd "$(dirname "$0")"
. ./gnustep_common.sh
SRC="$HERE/../../third_party/gnustep-eau-theme"
[ -f "$SRC/GNUmakefile" ] || { echo "error: $SRC not found -- run setup_third_party.sh" >&2; exit 1; }
[ -d "$ROOT$PREFIX/System/Library/Bundles/libgnustep-back-032.bundle" ] ||
    { echo "error: gnustep-back not installed -- run build_gnustep_back.sh first" >&2; exit 1; }
# Owners first, as in build_gnustep_back.sh: Eau calls Xlib directly
# (GSDisplayServer+Eau.m, -lX11), and Cairo/NSBezierPath drawing goes
# through gnustep-gui/back, which are already loaded in the app.
gs_setup_link "$SYS/libXext.dylib" "$SYS/libX11.dylib" "$SYS/libxcb.dylib" "$SYS/libXau.dylib" \
    "$SYS/libXdmcp.dylib" "${GS_BASE_SYSLIBS[@]}"
gs_build_env
X11_INC="$OUT/x11/include"
CPPFLAGS="${TARGET[*]} -D_FORTIFY_SOURCE=0 -I$GS_SYS_HEADERS -I$OUT/objc4_public_headers -I$X11_INC"
work="$G/eau-theme-src"
rsync -a --delete --exclude .git "$SRC/" "$work/"
GUILIBS="-L$GS_SYS_LIBS -lgnustep-gui -lgnustep-base"
(cd "$work" &&
 make DYLIB_EXTRA_FLAGS= GNUSTEP_INSTALLATION_DOMAIN=SYSTEM CPPFLAGS="$CPPFLAGS" \
     LDFLAGS="${TARGET[*]} $LINK $LIBFLAGS $GUILIBS" > make.out 2>&1) ||
    { grep -E "error:|Undefined|^ld: |^  \"" "$work/make.out" | grep -v "ld: warning" | head -40 || true
      echo "error: Eau build failed, see $work/make.out" >&2; exit 1; }
T="$ROOT$PREFIX/System/Library/Themes/Eau.theme"
rm -rf "$T"
# EAU_NO_TEST=1: the fork's GNUmakefile.postamble otherwise also builds and
# installs the Test/ alerttest fixture app, which only the upstream CI wants.
(cd "$work" && make install EAU_NO_TEST=1 DESTDIR="$ROOT" GNUSTEP_INSTALLATION_DOMAIN=SYSTEM > install.out 2>&1) ||
    { tail -20 "$work/install.out"; echo "error: Eau install failed" >&2; exit 1; }
gs_relocate_makefiles
[ -x "$T/Eau" ] || { find "$ROOT" -name 'Eau.theme' -maxdepth 8 >&2; echo "error: $T/Eau not installed" >&2; exit 1; }
gs_audit_tree "$T"
echo "installed $T"
