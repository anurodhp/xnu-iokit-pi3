#!/bin/bash
# QEMU check of btsetup(8), the helper behind the Bluetooth menu in Naples
# and the Bluetooth pane in Settings, against tools/bt_h4_responder.py's
# controller and keyboard (00:1f:20:4b:1d:01, Secure Simple Pairing with
# Passkey Entry):
#
#   1. clean: no keys, no devices (the image keeps /var/db).
#   2. status: power on, no devices.
#   3. scan: the keyboard is found with its kind and name (inquiry, Class of
#      Device, Remote Name Request).
#   4. pair: btpin, SDP + attach, bthidd connects, SSP: the passkey line
#      comes from bthcid's log, then "paired"; status lists the keyboard
#      as connected, with its name.
#   5. power off / on: the unit goes down and up, /etc/bluetooth.off
#      follows, and the keyboard reconnects with its stored key.
#   6. power off, then forget: the device, its key and its name are gone
#      (btsetup brings the unit up for it, without page scan, and down again).
#
# Needs the image of tools/bt_hid_qemu_test.sh, injected after
# build_bthidd.sh (btsetup.sh is copied by inject_into_sd_image.sh).
# Usage: tools/bt_setup_qemu_test.sh [image]
set -e
cd "$(dirname "$0")/.."
export SD_IMAGE="${1:-qemu/darwin_util_shots.img}"
SOCK=qemu/bt.sock
python3 tools/bt_h4_responder.py "$SOCK" > qemu/bt_responder.log 2>&1 &
RESP=$!
trap 'kill $RESP 2>/dev/null || true' EXIT
for i in $(seq 50); do [ -S "$SOCK" ] && break; sleep 0.1; done
export BT_SERIAL="unix:$SOCK"
export EXTRA_BOOT_ARGS="bt=1 ${BT_TEST_BOOT_ARGS:-}"
KB=00:1f:20:4b:1d:01
tools/boot_test.py --timeout 900 --login --log qemu/bt_setup_boot.log \
    --expect 'BT-SETUP DONE' --expect-fail 'panic' \
    --cmd "sleep 20; echo '== clean'; /usr/sbin/btsetup forget $KB; rm -f /var/db/btdevctl.plist /var/db/bthcid.keys /var/db/bluetooth-names /etc/bluetooth.off; kill -HUP \$(cat /var/run/bthidd.pid); sleep 10; : > /var/log/bthidd.log; : > /var/log/bthcid.log" \
    --cmd 'echo "== status"; /usr/sbin/btsetup status; echo "rc=$?"' \
    --cmd 'echo "== scan"; /usr/sbin/btsetup scan; echo "rc=$?"' \
    --cmd "echo '== pair'; /usr/sbin/btsetup pair $KB keyboard 'Fake Keyboard'; echo \"rc=\$?\"" \
    --cmd 'echo "== status after pairing"; /usr/sbin/btsetup status' \
    --cmd 'echo "== power off"; /usr/sbin/btsetup power off; echo "rc=$?"; /usr/sbin/btsetup status; ls /etc/bluetooth.off' \
    --cmd 'echo "== power on"; /usr/sbin/btsetup power on; echo "rc=$?"; ls /etc/bluetooth.off 2>&1; for i in $(seq 30); do /usr/sbin/btsetup status | grep -q "$(printf "\tconnected\t")" && break; sleep 1; done; echo "waited ${i}s"; /usr/sbin/btsetup status' \
    --cmd "echo '== forget (Bluetooth off)'; /usr/sbin/btsetup power off; /usr/sbin/btsetup forget $KB; echo \"rc=\$?\"; sleep 3; /usr/sbin/btsetup status; /usr/sbin/btconfig bcmbt0 | head -1; /usr/sbin/btsetup power on; /usr/bin/btkey -l -d bcmbt0; echo keys-rc=\$?; echo names:; cat /var/db/bluetooth-names 2>&1" \
    --cmd 'echo "== bthidd.log"; cat /var/log/bthidd.log; echo "== bthcid.log"; cat /var/log/bthcid.log; echo "BT-SETUP D""ONE"'
echo "== responder"; cat qemu/bt_responder.log
