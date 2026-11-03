#!/bin/bash
# QEMU check of the Bluetooth host stack (docs/bluetooth-stack-plan.md S1-S3):
# NetBSD's netbt in the kernel, BCM43430Bluetooth as its HCI unit bcmbt0,
# and the NetBSD userland over AF_BLUETOOTH sockets. A fake controller
# (tools/bt_h4_responder.py) sits on QEMU's PL011, the Pi 3's Bluetooth UART.
#
# The org.puredarwin.bluetooth job runs `bt up` (patch, speed) and then
# `btconfig bcmbt0 up`, which makes netbt reset and initialise the
# controller through the driver's TX ring and RX path. The test then lists
# the unit, runs an inquiry through netbt (the responder answers with one
# device), and queries the org.netbsd.sdpd job's sdpd over its local socket.
#
# Needs a kernel built with netbt (tools/build_xnu_7195.sh) and an image
# injected after build_bt.sh, fetch_bt_firmware.sh, build_libbluetooth.sh and
# build_netbt_tools.sh.
# Usage: tools/bt_netbt_qemu_test.sh [image]
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
tools/boot_test.py --timeout 900 --login --log qemu/bt_netbt_boot.log \
    --expect 'BT-NETBT DONE' --expect-fail 'panic' \
    --cmd 'sleep 20; echo "== bluetooth.log"; cat /var/log/bluetooth.log; echo "== bt info"; /usr/sbin/bt info' \
    --cmd 'echo "== btconfig -v"; /usr/sbin/btconfig -v; echo "rc=$?"' \
    --cmd 'echo "== inquiry"; /usr/sbin/btconfig bcmbt0 inquiry; echo "rc=$?"' \
    --cmd 'echo "== sdpd (org.netbsd.sdpd job)"; ps -ax -o pid,command | grep "[s]dpd"; /usr/bin/sdpquery -l browse; echo "rc=$?"; cat /var/log/sdpd.log' \
    --cmd 'echo "== btconfig after"; /usr/sbin/btconfig -v bcmbt0; echo "BT-NETBT D""ONE"'
echo "== responder"; cat qemu/bt_responder.log
