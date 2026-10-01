#!/bin/bash
# Builds the Eau theme (DAR-461), a Mac-like GNUstep theme, from
# third_party/gnustep-eau-theme (our fork anurodhp/gershwin-eau-theme,
# branch iokit-port, off gershwin-desktop/gershwin-eau-theme main, LGPL-2.1;
# the original Savannah plugins/themes/Eau is not on GitHub and savannah's
# viewvc/svn is not reachable by script), and installs it into
# libc_build/gnustep/root as /usr/GNUstep/System/Library/Themes/Eau.theme.
# Alternative theme (Rik is the default): GSTheme = "Eau"; in /etc/GNUstep/GlobalDefaults.plist
# (tools/userland_staging/x11_config/gnustep/GlobalDefaults.plist).
#
# Builds ONLY the bundle: it links the already-installed libgnustep-gui,
# libgnustep-base and the X11 libraries, and rebuilds none of them. The
# theme is an NSBundle that gnustep-gui dlopen()s, so bind_target_audit.sh
# is run on the bundle's executable (gs_audit_tree).
# run build_gnustep_host_tools.sh and build_gnustep_back.sh first.
cd "$(dirname "$0")"
. ./gnustep_common.sh
# EAU_NO_TEST=1: the fork's GNUmakefile.postamble otherwise also builds and
# installs the Test/ alerttest fixture app, which only the upstream CI wants.
gs_build_theme Eau "$HERE/../../third_party/gnustep-eau-theme" EAU_NO_TEST=1
