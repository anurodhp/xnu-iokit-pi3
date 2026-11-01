#!/bin/bash
# Builds Activity.app, a process/CPU/memory monitor (like htop, as a window) (our
# repo anurodhp/darwin-util, folder Activity/; ACTIVITY_SRC overrides) and
# installs it into libc_build/gnustep/root as
# /usr/GNUstep/System/Applications/Activity.app (/Applications/Activity.app on
# the image, stage_gnustep_mac_layout.sh).
# Pure AppKit. Its data (sysctl KERN_PROC_ALL/KERN_PROCARGS2, proc_pidinfo,
# host_processor_info, host_statistics64, mach_timebase_info, getpwuid, kill)
# all comes from the libSystem tier gs_build_app links already.
# Build, install and audit: gnustep_common.sh's gs_build_app.
cd "$(dirname "$0")"
. ./gnustep_common.sh
# Source: ACTIVITY_SRC, else the darwin-util checkout beside this repo
# (../darwin-util, the same level as ../naples and ../venice, where the apps
# are developed), else the pinned third_party/darwin-util clone
# (setup_third_party.sh).
if [ -z "${ACTIVITY_SRC:-}" ]; then
    if [ -f "$HERE/../../../darwin-util/Activity/GNUmakefile" ]; then ACTIVITY_SRC="$HERE/../../../darwin-util/Activity"
    else ACTIVITY_SRC="$HERE/../../third_party/darwin-util/Activity"; fi
fi
echo "Activity source: $ACTIVITY_SRC ($(git -C "$ACTIVITY_SRC" describe --tags --always --dirty 2>/dev/null || echo 'not a git checkout'))"
gs_build_app Activity "$ACTIVITY_SRC"
