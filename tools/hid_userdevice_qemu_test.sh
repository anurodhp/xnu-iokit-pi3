#!/bin/bash
# QEMU check for docs/bluetooth-stack-plan.md step H0: a userland HID
# keyboard and mouse (hid_userdevice_test, through IOHIDResource) reach
# IOHIDEventService, the path X's iohid input driver reads. hid_event_smoketest
# opens every IOHIDEventService and prints the keyboard and pointer events it
# gets; with the virtual devices alive it must show key usages 0x0b/0x0c ("h",
# "i") and relative pointer motion of +-20.
# Needs an image injected after build_hid_userdevice_test.sh and
# build_hid_event_smoketest.sh, and a kernel with the IOHIDResource
# personality (BCM2837Personalities.cpp).
# Usage: tools/hid_userdevice_qemu_test.sh [image]
set -e
cd "$(dirname "$0")/.."
export SD_IMAGE="${1:-qemu/darwin_util_shots.img}"
exec tools/boot_test.py --timeout 900 --login --log qemu/hid_userdevice_boot.log \
    --expect 'HID-H0 DONE' --expect-fail 'panic' \
    --cmd '/usr/local/bin/hid_userdevice_test 30 > /tmp/hud.log 2>&1 & sleep 3; echo "== hid_userdevice_test"; cat /tmp/hud.log' \
    --cmd '/usr/local/bin/hid_event_smoketest 15 2>&1 | tail -40' \
    --cmd 'sleep 25; echo "== hid_userdevice_test result"; cat /tmp/hud.log; echo "HID-H0 D""ONE"'
