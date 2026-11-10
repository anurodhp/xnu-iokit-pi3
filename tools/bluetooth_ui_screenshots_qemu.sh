#!/bin/bash
# Screenshots of the Bluetooth UI (Naples' menu bar panel and pairing dialog,
# Settings' Bluetooth pane) on the real desktop in QEMU, with
# tools/bt_h4_responder.py as the controller and a keyboard to pair: copies
# tools/bluetooth_ui_screenshots_guest.sh onto the image, boots it with bt=1,
# runs it as root, and writes the screenshots to OUT (default
# qemu/bluetooth_ui_shots/) as PNGs.
# Needs an image injected after build_naples.sh and build_turin.sh, with the
# Bluetooth tools (tools/bt_setup_qemu_test.sh's image) and the X server, xwd
# and x11_probe on it.
# Usage: tools/bluetooth_ui_screenshots_qemu.sh [image]
#   BT_ICON_X  x of the Bluetooth rune in the menu bar (else the guest searches)
set -e
cd "$(dirname "$0")/.."
IMG="${1:-qemu/darwin_util_shots.img}"
OUT="${OUT:-qemu/bluetooth_ui_shots}"
SHOTS="Desktop NaplesBluetooth NaplesPairing NaplesBluetoothPaired SettingsBluetooth"
[ -f "$IMG" ] || { echo "error: $IMG not found" >&2; exit 1; }

with_root() { # as darwin_util_screenshots_qemu.sh
    local disk slice
    disk=$(hdiutil attach -nomount "$IMG" | awk 'NR==1{print $1}')
    [ -n "$disk" ] || { echo "error: hdiutil attach failed" >&2; exit 1; }
    slice="${disk}s2"
    if ! diskutil mount "$slice" >/dev/null; then
        fsck_hfs -fy "/dev/r${slice#/dev/}" >/dev/null
        diskutil mount "$slice" >/dev/null
    fi
    MNT=$(diskutil info "$slice" | awk -F': +' '/Mount Point/{print $2}')
    "$@" || { hdiutil detach "$disk" >/dev/null; return 1; }
    diskutil unmount "$slice" >/dev/null 2>&1 || true
    hdiutil detach "$disk" >/dev/null
}
put_script() {
    [ -x "$MNT/usr/sbin/btsetup" ] || { echo "error: /usr/sbin/btsetup is not on $IMG -- re-inject" >&2; return 1; }
    rm -rf "$MNT/var/root/shots"
    mkdir -p "$MNT/var/root/shots"
    cp tools/bluetooth_ui_screenshots_guest.sh "$MNT/var/root/bluetooth_ui_shots.sh"
}
get_results() {
    local s
    mkdir -p "$OUT"
    cp "$MNT"/var/root/shots/*.log "$OUT/" 2>/dev/null || true
    for s in $SHOTS; do
        if [ -f "$MNT/var/root/shots/$s.xwd" ]; then
            python3 tools/xwd_to_png.py "$MNT/var/root/shots/$s.xwd" "$OUT/$s.png" | head -1
            echo "wrote $OUT/$s.png"
        else
            echo "missing: no $s screenshot (see $OUT/ and qemu/bluetooth_ui_shots_boot.log)"
        fi
    done
}

with_root put_script
SOCK=qemu/bt.sock
BT_TYPING_DELAY="${BT_TYPING_DELAY:-15}" python3 tools/bt_h4_responder.py "$SOCK" > qemu/bt_responder.log 2>&1 &
RESP=$!
trap 'kill $RESP 2>/dev/null || true' EXIT
for i in $(seq 50); do [ -S "$SOCK" ] && break; sleep 0.1; done
rc=0
SD_IMAGE="$IMG" BT_SERIAL="unix:$SOCK" EXTRA_BOOT_ARGS="bt=1" tools/boot_test.py --timeout 3600 --login \
    --log qemu/bluetooth_ui_shots_boot.log --expect 'SHOTS DONE' --expect-fail 'panic' \
    --cmd "sleep 20; BT_ICON_X=${BT_ICON_X:-} sh /var/root/bluetooth_ui_shots.sh 2>&1" || rc=$?
with_root get_results
exit $rc
