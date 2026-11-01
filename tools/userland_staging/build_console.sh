#!/bin/bash
# Builds Console.app, which shows the kernel message buffer or the system log live
# (our repo anurodhp/darwin-util, folder Console/; CONSOLE_SRC overrides) and
# installs it into libc_build/gnustep/root as
# /usr/GNUstep/System/Applications/Console.app (/Applications/Console.app on the
# image, stage_gnustep_mac_layout.sh).
# Pure AppKit; the kernel buffer comes through libsystem_kernel's __proc_info,
# which the libSystem tier gs_build_app links already lists.
# Build, install and audit: gnustep_common.sh's gs_build_app.
cd "$(dirname "$0")"
. ./gnustep_common.sh
# Source: CONSOLE_SRC, else the darwin-util checkout beside this repo
# (../darwin-util, the same level as ../naples and ../venice, where the apps
# are developed), else the pinned third_party/darwin-util clone
# (setup_third_party.sh).
if [ -z "${CONSOLE_SRC:-}" ]; then
    if [ -f "$HERE/../../../darwin-util/Console/GNUmakefile" ]; then CONSOLE_SRC="$HERE/../../../darwin-util/Console"
    else CONSOLE_SRC="$HERE/../../third_party/darwin-util/Console"; fi
fi
echo "Console source: $CONSOLE_SRC ($(git -C "$CONSOLE_SRC" describe --tags --always --dirty 2>/dev/null || echo 'not a git checkout'))"
gs_build_app Console "$CONSOLE_SRC"
