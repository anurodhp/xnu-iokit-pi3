#!/bin/bash
# Builds Milan.app, a small fast GNUstep file manager (Finder-style; the
# milan/ checkout, MILAN_SRC overrides) and installs it into
# libc_build/gnustep/root as /usr/GNUstep/System/Applications/Milan.app.
# Pure AppKit + GCD (directory loads and file operations run on dispatch
# queues, live refresh uses a kqueue vnode dispatch source); no extra libs.
# Build, install and audit: gnustep_common.sh's gs_build_app.
cd "$(dirname "$0")"
. ./gnustep_common.sh
gs_build_app Milan "${MILAN_SRC:-$HERE/../../milan}"
