#!/bin/bash
# Boots an SD image (default qemu/gl_test.img, prepared with
# tools/init_binary/inject_into_sd_image.sh) under QEMU, starts Xvfb and
# runs /usr/X11/bin/gl_smoketest (software OpenGL, docs/gpu-opengl-plan.md).
# Exit 0 only if the guest printed "GLTEST PASS". Log: qemu/gl_boot.log.
# Usage: tools/gl_qemu_test.sh [image]
set -e
cd "$(dirname "$0")/.."
export SD_IMAGE="${1:-qemu/gl_test.img}"
exec tools/boot_test.py --timeout 700 --login --log qemu/gl_boot.log \
    --expect 'GLTEST PASS' --expect-fail 'panic' --expect-fail 'GLTEST FAIL' \
    --cmd '/usr/X11/bin/Xvfb :0 -screen 0 320x240x24 > /tmp/xvfb.log 2>&1 &' \
    --cmd 'sleep 15' \
    --cmd '/usr/X11/bin/gl_smoketest :0 2>&1; echo glrc=$?'
