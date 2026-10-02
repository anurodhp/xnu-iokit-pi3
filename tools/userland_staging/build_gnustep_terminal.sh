#!/bin/bash
# Builds Terminal.app (DAR-394), GNUstep's terminal emulator, from
# third_party/gnustep-gap (our fork anurodhp/gap, branch iokit-port, off
# gnustep/gap master: system-apps/Terminal), and installs it into
# libc_build/gnustep/root as /usr/GNUstep/System/Applications/Terminal.app
# (the build layout; on the image it is /Applications/Terminal.app, see
# stage_gnustep_mac_layout.sh). Run it on the target under an X server:
# /Applications/Terminal.app/Terminal
#
# It starts the shell with forkpty() (TerminalView.m), which on Darwin is
# libsystem_c's (Libc util/pty.c, with login_tty.c and stdlib/grantpt.c;
# build_libc_gen.sh / build_libsystem_c.sh), over the kernel's /dev/ptmx.
# The fork adds the Darwin <util.h> branch the upstream source lacks.
# Character-set conversion goes through libiconv.
# Build, install and audit: gnustep_common.sh's gs_build_app.
cd "$(dirname "$0")"
. ./gnustep_common.sh
# gs_build_app checks these; the line is here so gen_build_order.py sees the
# edge: run build_gnustep_host_tools.sh and build_gnustep_back.sh first.
for s in _forkpty _openpty _grantpt; do
    nm -gU "$SYS/libsystem_c.dylib" | grep -q " $s$" ||
        { echo "error: libsystem_c.dylib does not export $s -- run build_libc_gen.sh and build_libsystem_c.sh first" >&2; exit 1; }
done
gs_build_app Terminal "$HERE/../../third_party/gnustep-gap/system-apps/Terminal"
