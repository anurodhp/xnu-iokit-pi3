#!/bin/bash
# Boots an image with Rome.app (build_libvterm.sh + build_rome.sh, then
# inject_into_sd_image.sh) and runs it under Xvfb with both renderers:
# the CPU one (x11, the default) and OpenGL (softpipe under QEMU). Each run
# scrolls 3000 lines through the terminal and quits when the command exits;
# ROME_STATS=1 prints the per-frame cost (rome-stats: ...).
# Usage: tools/rome_qemu_test.sh [image]
set -e
cd "$(dirname "$0")/.."
export SD_IMAGE="${1:-qemu/hfs_root_test.img}"
R=/Applications/Rome.app/Rome
exec tools/boot_test.py --timeout 1500 --login --log qemu/rome_boot.log \
    --expect 'rome-stats: renderer=x11' --expect 'rome-x11-rc=0' \
    --expect 'rome-stats: renderer=gl' --expect 'rome-gl-rc=0' \
    --expect-fail 'panic' \
    --cmd '/usr/X11/bin/Xvfb :0 -screen 0 1024x768x24 -nolisten tcp > /tmp/xvfb.log 2>&1 &' \
    --cmd 'sleep 40' \
    --cmd "DISPLAY=:0 ROME_STATS=1 $R -RomeCommand 'seq 1 3000' -RomeQuitOnExit YES 2>&1 | grep rome; echo rome-x11-rc=\${PIPESTATUS[0]}" \
    --cmd "DISPLAY=:0 ROME_STATS=1 $R -RomeRenderer gl -RomeCommand 'seq 1 3000' -RomeQuitOnExit YES 2>&1 | grep rome; echo rome-gl-rc=\${PIPESTATUS[0]}"
