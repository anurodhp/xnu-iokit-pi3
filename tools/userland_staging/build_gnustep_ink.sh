#!/bin/bash
# Builds Ink.app (DAR-390/DAR-394), GNUstep's small document-based text
# editor (NSDocument, NSTextView, RTF/plain text), from
# third_party/gnustep-examples (gnustep/tests-examples, gui/Ink), and
# installs it into libc_build/gnustep/root as
# /usr/GNUstep/System/Applications/Ink.app. It is the first AppKit app on
# this port: run it on the target with `openapp Ink` (or
# /usr/GNUstep/System/Applications/Ink.app/Ink) under an X server.
#
# Build, install and audit: gnustep_common.sh's gs_build_app.
cd "$(dirname "$0")"
. ./gnustep_common.sh
# gs_build_app checks these; the line is here so gen_build_order.py sees the
# edge: run build_gnustep_host_tools.sh and build_gnustep_back.sh first.
gs_build_app Ink "$HERE/../../third_party/gnustep-examples/gui/Ink"
