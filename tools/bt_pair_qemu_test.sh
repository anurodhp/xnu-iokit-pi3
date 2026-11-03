#!/bin/bash
# QEMU check of legacy (PIN) pairing (docs/bluetooth-stack-plan.md S4):
# netbt asks the controller to authenticate a link whose L2CAP sockets want
# it (SO_L2CAP_LM auth, from the device's btdevctl link-mode), and bthcid(8)
# answers the controller's Link_Key_Request and PIN_Code_Request over its
# HCI socket. tools/bt_h4_responder.py's keyboard accepts only PIN 1234 and
# then issues a link key.
#
#   1. clean: SSP off (the boot job turns it on; tools/bt_ssp_qemu_test.sh
#      covers it), no keys, keyboard detached (the image keeps /var/db).
#   2. btpin hands bthcid the PIN for the keyboard. `btdevctl -A` queries
#      the keyboard's HID service over SDP (the responder's SDP server),
#      caches it (link mode "encrypt": a keyboard) and has bthidd attach it;
#      bthidd connects: no key -> PIN -> paired, encrypted; bthcid stores
#      the key; the HID device comes up.
#   3. btkey lists the stored key; `btdevctl -D`, then `-A` again (from the
#      cache, no SDP): this time the key is presented and accepted, no PIN.
#   4. hid_event_smoketest must see the keyboard's keys.
#
# Needs the image of tools/bt_hid_qemu_test.sh.
# Usage: tools/bt_pair_qemu_test.sh [image]
set -e
cd "$(dirname "$0")/.."
export SD_IMAGE="${1:-qemu/darwin_util_shots.img}"
SOCK=qemu/bt.sock
python3 tools/bt_h4_responder.py "$SOCK" > qemu/bt_responder.log 2>&1 &
RESP=$!
trap 'kill $RESP 2>/dev/null || true' EXIT
for i in $(seq 50); do [ -S "$SOCK" ] && break; sleep 0.1; done
export BT_SERIAL="unix:$SOCK"
export EXTRA_BOOT_ARGS="bt=1 ${BT_TEST_BOOT_ARGS:-}"   # e.g. BT_TEST_BOOT_ARGS=netbt-debug=1
tools/boot_test.py --timeout 900 --login --log qemu/bt_pair_boot.log \
    --expect 'BT-PAIR DONE' --expect-fail 'panic' \
    --cmd 'sleep 20; echo "== clean (and SSP off: the boot job turns it on)"; /usr/sbin/btconfig bcmbt0 -ssp; rm -f /var/db/btdevctl.plist /var/db/bthcid.keys; kill -HUP $(cat /var/run/bthidd.pid); sleep 15; : > /var/log/bthidd.log; : > /var/log/bthcid.log' \
    --cmd 'echo "== pair"; /usr/bin/btpin -d bcmbt0 -a 00:1f:20:4b:1d:01 -p 1234; echo btpin rc=$?; /usr/sbin/btdevctl -A -v -d bcmbt0 -a 00:1f:20:4b:1d:01 -s HID; echo btdevctl rc=$?; sleep 8; cat /var/log/bthidd.log; cat /var/log/bthcid.log' \
    --cmd 'echo "== keys"; /usr/bin/btkey -l -d bcmbt0; echo rc=$?' \
    --cmd 'echo "== reconnect with the stored key"; /usr/sbin/btdevctl -D -d bcmbt0 -a 00:1f:20:4b:1d:01 -s HID; sleep 15; /usr/sbin/btdevctl -A -d bcmbt0 -a 00:1f:20:4b:1d:01 -s HID; sleep 4' \
    --cmd '/usr/local/bin/hid_event_smoketest 10 2>&1 | tail -4' \
    --cmd 'echo "== bthidd.log"; cat /var/log/bthidd.log; echo "== bthcid.log"; cat /var/log/bthcid.log; echo "BT-PAIR D""ONE"'
echo "== responder"; cat qemu/bt_responder.log
