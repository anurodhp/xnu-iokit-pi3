#!/bin/bash
# Boots an image with Venice.app (build_libmpv.sh's media stack +
# build_venice.sh, then inject_into_sd_image.sh) and plays a clip under Xvfb:
# from a local file with both renderers (gl: softpipe under QEMU; sw), and as
# an http:// stream from the host (QEMU user networking reaches the host at
# 10.0.2.2). Each run quits at the end of the clip (-VeniceQuitAtEnd YES);
# VENICE_STATS=1 prints the per-frame cost (venice-stats: ...). Audio is
# off (ao=null): a run through audiod under QEMU did not finish (probably
# audiod not draining without pwm-sim=1; not confirmed); audio is tested on
# the Pi.
#
# The clip must be on the image as /venice_test.mp4, and served by the host:
#   ffmpeg -f lavfi -i testsrc=size=320x240:rate=25 -f lavfi -i sine=frequency=440 \
#          -t 4 -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest venice_test.mp4
#   (cd <dir with the clip> && python3 -m http.server 8765) &
# Usage: tools/venice_qemu_test.sh [image]
set -e
cd "$(dirname "$0")/.."
export SD_IMAGE="${1:-qemu/hfs_root_test.img}"
V=/Applications/Venice.app/Venice
exec tools/boot_test.py --timeout 1500 --login --log qemu/venice_boot.log \
    --expect 'venice-gl-rc=0' --expect 'venice-sw-rc=0' --expect 'venice-http-rc=0' \
    --expect-fail 'panic' \
    --cmd '/usr/X11/bin/Xvfb :0 -screen 0 1024x768x24 -nolisten tcp > /tmp/xvfb.log 2>&1 &' \
    --cmd 'sleep 40' \
    --cmd "DISPLAY=:0 VENICE_STATS=1 $V -VeniceMPVOptions ao=null -VeniceQuitAtEnd YES /venice_test.mp4 2>&1 | grep -i venice; echo venice-gl-rc=\${PIPESTATUS[0]}" \
    --cmd "DISPLAY=:0 VENICE_STATS=1 $V -VeniceRenderer sw -VeniceMPVOptions ao=null -VeniceQuitAtEnd YES /venice_test.mp4 2>&1 | grep -i venice; echo venice-sw-rc=\${PIPESTATUS[0]}" \
    --cmd "DISPLAY=:0 VENICE_STATS=1 $V -VeniceRenderer sw -VeniceMPVOptions ao=null -VeniceQuitAtEnd YES http://10.0.2.2:8765/venice_test.mp4 2>&1 | grep -i venice; echo venice-http-rc=\${PIPESTATUS[0]}"
