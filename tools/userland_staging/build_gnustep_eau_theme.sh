#!/bin/bash
# Builds the Eau theme (DAR-461), a Mac-like GNUstep theme, from
# third_party/gnustep-eau-theme (our fork anurodhp/gershwin-eau-theme,
# branch iokit-port, off gershwin-desktop/gershwin-eau-theme main, LGPL-2.1;
# the original Savannah plugins/themes/Eau is not on GitHub and savannah's
# viewvc/svn is not reachable by script), and installs it into
# libc_build/gnustep/root as /usr/GNUstep/System/Library/Themes/Eau.theme
# (/System/Library/Themes/Eau.theme on the image: stage_gnustep_mac_layout.sh).
# The default and only GNUstep theme: GSTheme = "Eau"; in /etc/GNUstep/GlobalDefaults.plist
# (tools/userland_staging/x11_config/gnustep/GlobalDefaults.plist). EauInfo.plist
# sets no NSMenuInterfaceStyle, so GlobalDefaults' NSInterfaceStyleDefault =
# NSMacintoshInterfaceStyle applies (horizontal top menu bar; NSMenu+Eau.m).
#
# Blocks: Eau uses dispatch_async/dispatch_sync/dispatch_once with block
# literals and two NSNotificationCenter usingBlock: observers; all reach
# libclosure/libdispatch/gnustep-base through C Block_copy/Block_release only,
# never an ObjC message to a block: third_party/libdispatch src/init.c:1436
# (_dispatch_Block_copy = Block_copy), src/inline_internal.h:2730 (async
# continuation); third_party/gnustep-base Source/NSNotificationCenter.m:614,657
# (observer Block_copy), Headers/GNUstepBase/GSBlocks.h:142 (-> _Block_copy).
# No NSTimer block API, no -copy on a block (grep of the fork, 7bc5b9d). The fork's -O2 (GNUmakefile) is what
# keeps ARC from objc_retain()ing global blocks, which crashes here.
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
