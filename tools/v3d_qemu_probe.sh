#!/bin/bash
# Boots qemu/gl_test.img (prepared by tools/init_binary/inject_into_sd_image.sh)
# and runs /usr/X11/bin/v3dprobe in the guest. QEMU raspi3 has no V3D, so the
# expected outcome is the graceful "cannot open BCM2837V3D (No such device)"
# with exit status 2 -- it checks that the whole userland path (dyld,
# libdrm.2.dylib, libiokituser, IOServiceGetMatchingService) works, not the GPU.
# Usage: [QEMU_DTB=devicetrees/rpi3-v3dtest.dtb] tools/v3d_qemu_probe.sh
set -e
cd "$(dirname "$0")/.."
export SD_IMAGE="${SD_IMAGE:-qemu/gl_test.img}"
exec tools/boot_test.py --timeout 300 --login --log qemu/v3dprobe_boot.log \
    --expect 'v3dprobe-rc=' --expect-fail 'panic' \
    --cmd '/usr/X11/bin/v3dprobe; echo v3dprobe-rc=$?'
