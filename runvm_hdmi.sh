#!/bin/bash
# Boots exactly like runvm.sh (same kernel, SD image, USB devices, serial
# console on this terminal), but with the HDMI framebuffer shown in a QEMU
# window.
#
#   ./runvm.sh        serial only (headless, what tools/boot_test.py uses)
#   ./runvm_hdmi.sh   serial + HDMI window
#
# hdmi_console=1 is the kernel boot-arg (osfmk/arm/arm_init.c) that asks the
# VideoCore mailbox for a 640x480x32 framebuffer and mirrors all console
# output onto it as a text console, in addition to serial. It also gives
# IOGraphics' IOBootFramebuffer a real framebuffer to attach to (DAR-331).
# Without it the kernel never sets up video at all.
#
# All runvm.sh knobs still apply (SD_IMAGE, ROOT_DEV, USB_DEVICES,
# EXTRA_BOOT_ARGS). QEMU_DISPLAY defaults to cocoa (macOS); override it,
# e.g. QEMU_DISPLAY=sdl, elsewhere.
set -e
cd "$(dirname "$0")"
export QEMU_DISPLAY="${QEMU_DISPLAY:-cocoa}"
export EXTRA_BOOT_ARGS="hdmi_console=1${EXTRA_BOOT_ARGS:+ $EXTRA_BOOT_ARGS}"
exec ./runvm.sh "$@"
