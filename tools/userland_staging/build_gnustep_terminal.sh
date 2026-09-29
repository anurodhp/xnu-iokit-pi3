#!/bin/bash
# Builds Terminal.app (DAR-394), GNUstep's terminal emulator, from
# third_party/gnustep-gap (our fork anurodhp/gap, branch iokit-port, off
# gnustep/gap master: system-apps/Terminal), and installs it into
# libc_build/gnustep/root as /usr/GNUstep/System/Applications/Terminal.app.
# Run it on the target under an X server:
# /usr/GNUstep/System/Applications/Terminal.app/Terminal
#
# It starts the shell with forkpty() (TerminalView.m), which on Darwin is
# libsystem_c's (Libc util/pty.c, with login_tty.c and stdlib/grantpt.c;
# build_libc_gen.sh / build_libsystem_c.sh), over the kernel's /dev/ptmx.
# The fork adds the Darwin <util.h> branch the upstream source lacks.
# Character-set conversion goes through libiconv.
# Build, install and audit: gnustep_common.sh's gs_build_app.
cd "$(dirname "$0")"
. ./gnustep_common.sh
for s in _forkpty _openpty _grantpt; do
    nm -gU "$SYS/libsystem_c.dylib" | grep -q " $s$" ||
        { echo "error: libsystem_c.dylib does not export $s -- run build_libc_gen.sh and build_libsystem_c.sh first" >&2; exit 1; }
done
gs_build_app Terminal "$HERE/../../third_party/gnustep-gap/system-apps/Terminal"
