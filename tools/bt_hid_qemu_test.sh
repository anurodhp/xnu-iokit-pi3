#!/bin/bash
# QEMU check of the Bluetooth HID host (docs/bluetooth-stack-plan.md S5):
# bthidd over netbt's L2CAP sockets, feeding IOKit through IOHIDUserDevice.
# tools/bt_h4_responder.py plays the controller and a Bluetooth keyboard
# (00:1f:20:4b:1d:01) that reconnects when the host turns on page scan.
#
# The keyboard may already be attached from an earlier run (the image keeps
# /var/db/btdevctl.plist), in which case it connects at boot. The test first
# detaches it (removes the cache entry, SIGHUP): bthidd must close both
# channels, which the responder logs as host L2CAP DisconnReqs, and the idle
# link then expires. Then it writes the keyboard's btdevctl(8) cache entry (what
# `btdevctl -A -a 00:1f:20:4b:1d:01 -d bcmbt0 -s HID` stores after an SDP
# query: boot-keyboard descriptor, PSMs 0x11/0x13, attached) -- the
# responder has no SDP server -- signals bthidd to reread it, toggles page
# scan, and runs hid_event_smoketest, which must see the keyboard's "h" and
# "i" (usages 0x0b, 0x0c) arrive as IOHIDEventService key events.
#
# Needs the kernel and image of tools/bt_netbt_qemu_test.sh plus
# build_libusbhid.sh, build_netbt_tools.sh (btdevctl), build_bthidd.sh and
# build_hid_event_smoketest.sh before injecting.
# Usage: tools/bt_hid_qemu_test.sh [image]
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
FIXTURE="<?xml version=\"1.0\" encoding=\"UTF-8\"?><plist version=\"1.0\"><dict><key>btdevctl-version</key><integer>1</integer><key>52:54:00:12:34:57</key><dict><key>00:1f:20:4b:1d:01</key><dict><key>HID</key><dict><key>device-type</key><string>bthidev</string><key>attached</key><true/><key>control-psm</key><integer>17</integer><key>interrupt-psm</key><integer>19</integer><key>vendor-id</key><integer>1452</integer><key>product-id</key><integer>32771</integer><key>descriptor</key><data>BQEJBqEBBQcZ4CnnFQAlAXUBlQiBApUBdQiBAZUGdQgVACVlBQcZACllgQDA</data></dict></dict></dict></dict></plist>"
tools/boot_test.py --timeout 900 --login --log qemu/bt_hid_boot.log \
    --expect 'BT-HID DONE' --expect-fail 'panic' \
    --cmd 'sleep 20; /usr/sbin/btconfig -v bcmbt0; ps -ax -o pid,command | grep "[b]thidd"; echo "== bthidd.log at boot"; cat /var/log/bthidd.log; : > /var/log/bthidd.log' \
    --cmd 'echo "== detach: bthidd must close the channels (host L2CAP DisconnReq)"; rm -f /var/db/btdevctl.plist; kill -HUP $(cat /var/run/bthidd.pid); sleep 15; cat /var/log/bthidd.log; : > /var/log/bthidd.log' \
    --cmd "echo '== attach'; printf '%s' '$FIXTURE' > /var/db/btdevctl.plist; kill -HUP \$(cat /var/run/bthidd.pid); sleep 2; /usr/sbin/btconfig bcmbt0 -pscan; sleep 1; /usr/sbin/btconfig bcmbt0 pscan" \
    --cmd 'sleep 4; /usr/local/bin/hid_event_smoketest 15 2>&1 | tail -25' \
    --cmd 'echo "== bthidd.log since"; cat /var/log/bthidd.log; echo "BT-HID D""ONE"'
echo "== responder"; cat qemu/bt_responder.log
