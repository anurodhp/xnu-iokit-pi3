#!/bin/bash
# Boots qemu/gl_test.img and runs the OpenGL smoketests under Xvfb:
# gl_smoketest (GL 3.3 compat + GLSL), gles2_smoketest (ES2 context through GLX +
# libGLESv2) and epoxy_smoketest (libepoxy dispatch). Expects all three PASS.
# Needs libGL built with MESA_GLES2=enabled build_mesa.sh, build_libepoxy.sh,
# the three build_*_smoketest.sh scripts, and an image injected afterwards.
# Usage: tools/gl_suite_qemu_test.sh [image]
set -e
cd "$(dirname "$0")/.."
export SD_IMAGE="${1:-qemu/gl_test.img}"
exec tools/boot_test.py --timeout 600 --login --log qemu/gl_suite_boot.log \
    --expect 'GLTEST PASS' --expect 'GLES2TEST PASS' --expect 'EPOXYTEST PASS' \
    --expect-fail 'panic' --expect-fail 'GLTEST FAIL' --expect-fail 'GLES2TEST FAIL' --expect-fail 'EPOXYTEST FAIL' \
    --cmd '/usr/X11/bin/Xvfb :0 -screen 0 320x240x24 > /tmp/xvfb.log 2>&1 &' \
    --cmd 'sleep 15' \
    --cmd '/usr/X11/bin/gl_smoketest :0 2>&1; echo gl-rc=$?' \
    --cmd '/usr/X11/bin/gles2_smoketest :0 2>&1; echo gles2-rc=$?' \
    --cmd '/usr/X11/bin/epoxy_smoketest :0 2>&1; echo epoxy-rc=$?'
