#!/bin/bash
# DARW-309: USB keyboard/mouse hot-plug under real Xorg, on QEMU.
# Copies tools/x11_hotplug_verify_guest.sh and the freshly built iohid_drv.so
# onto the image, boots with the HDMI framebuffer (display none) and
# hid-legacy-shim=1, and plugs/unplugs usb-kbd and usb-mouse via qemu/mon.sock
# at the guest's HPM_* markers. Asserts on the guest's RESULT lines.
# Needs an image injected by inject_into_sd_image.sh (Xorg, xev, iohid_drv.so).
# Usage: tools/x11_hotplug_qemu_test.sh [image]
set -e
cd "$(dirname "$0")/.."
IMG="${1:-qemu/hfs_root_test.img}"
LOG="${LOG:-qemu/x11_hotplug.log}"
[ -f "$IMG" ] || { echo "error: $IMG not found" >&2; exit 1; }
DRV=tools/userland_staging/libc_build/x11/server/root/usr/X11/lib/xorg/modules/input/iohid_drv.so

with_root() {
    local disk slice
    disk=$(hdiutil attach -imagekey diskimage-class=CRawDiskImage -nomount "$IMG" | awk 'NR==1{print $1}')
    slice="${disk}s2"
    diskutil mount "$slice" >/dev/null
    MNT=$(diskutil info "$slice" | awk -F': +' '/Mount Point/{print $2}')
    "$@" || { diskutil eject "$disk" >/dev/null; return 1; }
    diskutil eject "$disk" >/dev/null
}
put() {
    mkdir -p "$MNT/var/root"
    cp tools/x11_hotplug_verify_guest.sh "$MNT/var/root/x11_hotplug.sh"
    [ -f "$DRV" ] && cp "$DRV" "$MNT/usr/X11/lib/xorg/modules/input/iohid_drv.so"
    # The session job would also start X; this test runs Xorg itself.
    touch "$MNT/etc/x11-session.disabled"
}
with_root put

KB='device_add usb-kbd,id=kb1,bus=usb-bus.0,port=1.2'
MS='device_add usb-mouse,id=ms1,bus=usb-bus.0,port=1.3'
QEMU_DISPLAY=none EXTRA_BOOT_ARGS="hdmi_console=1 hid-legacy-shim=1" SD_IMAGE="$IMG" \
tools/boot_test.py --timeout "${TIMEOUT:-420}" --log "$LOG" --login \
    --cmd 'sh /var/root/x11_hotplug.sh' --expect 'HPM_END' --expect-fail 'HPM_XORG_DEAD' \
    --on-match "HPM_A_GO=$KB;$MS" \
    --on-match "HPM_A_TYPE=sendkey shift-a;mouse_move 20 10;mouse_button 1;mouse_button 0" \
    --on-match "HPM_B_GO=device_del kb1;device_del ms1" \
    --on-match "HPM_C_GO=$KB;$MS" \
    --on-match "HPM_C_TYPE=sendkey shift-b;mouse_move 20 10;mouse_button 1;mouse_button 0"
rc=$?
sed 's/\x1b\[[0-9;]*m//g' "$LOG" | grep -E "^RESULT|HPM_|iohid|Xorg" | head -60
exit $rc
