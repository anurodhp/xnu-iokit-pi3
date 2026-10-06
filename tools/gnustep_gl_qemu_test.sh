#!/bin/bash
# Boots qemu/gl_test.img and runs GLTest.app (NSOpenGLView through
# gnustep-back's GLX glue on Mesa's libGL, softpipe under QEMU) under Xvfb.
# Expects GSGLTEST PASS and prints a frame rate. Needs build_mesa.sh,
# build_gnustep_back.sh (GLX), build_gnustep_gltest.sh and an image injected
# afterwards. Usage: tools/gnustep_gl_qemu_test.sh [image]
set -e
cd "$(dirname "$0")/.."
export SD_IMAGE="${1:-qemu/gl_test.img}"
exec tools/boot_test.py --timeout 900 --login --log qemu/gnustep_gl_boot.log \
    --expect 'GSGLTEST PASS' --expect 'gsgl-rc=' \
    --expect-fail 'panic' --expect-fail 'GSGLTEST FAIL' \
    --cmd '/usr/X11/bin/Xvfb :0 -screen 0 640x480x24 > /tmp/xvfb.log 2>&1 &' \
    --cmd 'sleep 15' \
    --cmd 'DISPLAY=:0 /Applications/GLTest.app/GLTest -frames 30 2>&1 | grep -v "^20[0-9][0-9]-"; echo gsgl-rc=$?'
