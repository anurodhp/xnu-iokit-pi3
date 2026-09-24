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
# It also plugs a USB keyboard (QEMU usb-kbd) into port 2 of the stand-in
# LAN9514 hub, the way a real keyboard sits behind the Pi 3's hub, and sets
# hid-legacy-shim=1. That is Apple's own IOHIDEventService boot-arg
# (IOHIDEventService.cpp:311) that enables the kernel keyboard shim which
# feeds key presses to the console (IOHIDSystem -> cons_cinput). Without it
# the shim is off outside single-user mode. Click in the QEMU window to type
# on it. The serial console keeps working alongside (DAR-245).
#
# All runvm.sh knobs still apply (SD_IMAGE, ROOT_DEV, USB_DEVICES,
# EXTRA_BOOT_ARGS). QEMU_DISPLAY defaults to cocoa (macOS); override it,
# e.g. QEMU_DISPLAY=sdl, elsewhere.
set -e
cd "$(dirname "$0")"
export QEMU_DISPLAY="${QEMU_DISPLAY:-cocoa}"
export EXTRA_BOOT_ARGS="hdmi_console=1 hid-legacy-shim=1${EXTRA_BOOT_ARGS:+ $EXTRA_BOOT_ARGS}"
export EXTRA_USB_DEVICES="${EXTRA_USB_DEVICES-"-device usb-kbd,bus=usb-bus.0,port=1.2"}"
exec ./runvm.sh "$@"
