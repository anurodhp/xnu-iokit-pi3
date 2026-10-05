#!/bin/bash
# Hang-recovery test against the QEMU V3D model (hw/misc/bcm2835_v3d.c):
# the model never completes the first render job (property hang-job=1,
# hang-thread=1). Expected in the guest: the driver's hang detector notices
# (~200 ms), logs "GPU hang, resetting", power-cycles the block, fails the
# job; the first v3dprobe therefore reports a hang (and not-cleared pixels);
# a second v3dprobe run on the recovered GPU passes.
# Usage: tools/v3d_qemu_hang_test.sh   (needs the injected qemu/gl_test.img)
set -e
cd "$(dirname "$0")/.."
export SD_IMAGE="${SD_IMAGE:-qemu/gl_test.img}"
export QEMU_DTB="${QEMU_DTB:-devicetrees/rpi3-v3dtest.dtb}"
export QEMU_EXTRA_ARGS="-global bcm2835-v3d.hang-job=1 -global bcm2835-v3d.hang-thread=1 -d guest_errors -D qemu/v3d_guest_errors.log"
exec tools/boot_test.py --timeout 400 --login --log qemu/v3dhang_boot.log \
    --expect 'second-rc=' --expect-fail 'panic' \
    --cmd '/usr/X11/bin/v3dprobe; echo first-rc=$?' \
    --cmd '/usr/X11/bin/v3dprobe; echo second-rc=$?'
