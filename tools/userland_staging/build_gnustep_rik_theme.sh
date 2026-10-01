#!/bin/bash
# Builds the Rik theme (DAR-461), GNUstep's flat Mac-like theme that the
# Window Maker Rik.themed (gnustep/plugins-sessions-windowmaker) matches,
# from third_party/gnustep-rik-theme (our fork anurodhp/rik.theme, branch
# iokit-port, off AlessandroSangiuliano/rik.theme master fbb9944, LGPL-2.1,
# itself the maintained descendant of mclarenlabs/nongio-zz rik.theme), and
# installs it into libc_build/gnustep/root as
# /usr/GNUstep/System/Library/Themes/Rik.theme. This is the default
# (GSTheme = "Rik"; in x11_config/gnustep/GlobalDefaults.plist); to switch
# to Eau use GSTheme = "Eau"; (build_gnustep_eau_theme.sh).
# Bundle recipe: gnustep_common.sh's gs_build_theme.
# run build_gnustep_host_tools.sh and build_gnustep_back.sh first.
cd "$(dirname "$0")"
. ./gnustep_common.sh
gs_build_theme Rik "$HERE/../../third_party/gnustep-rik-theme"
