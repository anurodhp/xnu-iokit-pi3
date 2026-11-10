#!/bin/sh
# Guest side of tools/bluetooth_ui_screenshots_qemu.sh (copied to
# /var/root/bluetooth_ui_shots.sh, run as root in the QEMU guest, with
# tools/bt_h4_responder.py as the controller and its keyboard in range).
# Brings up the real desktop on Xvfb (gsdm's Xsession: Window Maker, Naples,
# Milan), then drives the Bluetooth UI as a person would, with a screenshot
# (xwd) of the whole screen into /var/root/shots/<name>.xwd after each step:
#   NaplesBluetooth         the menu bar's Bluetooth panel, devices in range
#   NaplesPairing           a click on the keyboard: the passkey dialog
#   NaplesBluetoothPaired   the panel again: the keyboard under My Devices
#   SettingsBluetooth       Bluetooth Settings…: Settings' Bluetooth pane
# Under QEMU/TCG GNUstep needs minutes per step, so each one waits for its
# window (x11_probe tree) and then lets it draw.
T=/var/root/shots
X=/usr/X11/bin
GEOM=${GEOM:-1280x800}
WAIT=${WAIT:-900}
SETTLE=${SETTLE:-45}
KB=00:1f:20:4b:1d:01
export DISPLAY=:0 HOME=/var/root

echo "== clean Bluetooth state"
/usr/sbin/btsetup forget $KB
rm -f /var/db/btdevctl.plist /var/db/bthcid.keys /var/db/bluetooth-names /etc/bluetooth.off
kill -HUP "$(cat /var/run/bthidd.pid)" 2>/dev/null
/usr/sbin/btsetup power on
/usr/sbin/btsetup status

echo "== Xvfb $GEOM"
$X/Xvfb :0 -screen 0 ${GEOM}x24 -nolisten tcp > $T/xvfb.log 2>&1 &
i=0
until $X/x11_probe tree > /dev/null 2>&1; do
    sleep 2; i=$((i + 2))
    [ $i -ge 120 ] && { echo "Xvfb did not start"; cat $T/xvfb.log; echo "SHOTS DONE"; exit 1; }
done

wait_window() { # <pattern>: until a viewable window whose name matches is mapped
    i=0
    while [ $i -lt $WAIT ]; do
        $X/x11_probe tree 2>/dev/null | grep ' viewable' | grep -q "name=$1" && break
        sleep 5; i=$((i + 5))
    done
    echo "window '$1' after ${i}s"
    [ $i -lt $WAIT ]
}
# the Bluetooth panel: Naples' only 272-pixel-wide window hanging from the bar
# (Wi-Fi's is as wide, but QEMU has no Wi-Fi). Prints WxH+X+Y.
panel() { $X/x11_probe tree 2>/dev/null | grep ' viewable' | grep -E '^  0x[0-9a-f]+ 272x[0-9]+\+[0-9]+\+2[0-9] ' | head -1 | sed 's/^ *[^ ]* \([^ ]*\) .*/\1/'; }
shot() { $X/xwd -root -out $T/$1.xwd; echo "shot $1 rc=$?"; }

echo "== desktop (gsdm's Xsession: Window Maker, Naples, Milan)"
sh /etc/gsdm/Xsession > $T/xsession.log 2>&1 &
date
# Naples' menu bar: an untitled window the width of the screen, 22 high, at the top
i=0
until $X/x11_probe tree 2>/dev/null | grep ' viewable' | grep -q " ${GEOM%%x*}x22+0+0 "; do
    sleep 5; i=$((i + 5))
    [ $i -ge $WAIT ] && { echo "the menu bar did not appear"; break; }
done
echo "menu bar after ${i}s"; date
ps -ax -o pid,command | grep -E '[w]maker|[N]aples|[M]ilan'
sleep ${DESKTOP_SETTLE:-120}
$X/x11_probe tree > $T/tree-desktop.log 2>&1
shot Desktop
export GNUSTEP_CONFIG_FILE=/etc/GNUstep/GNUstep.conf
[ -f /System/Library/Makefiles/GNUstep.sh ] && . /System/Library/Makefiles/GNUstep.sh

echo "== find the Bluetooth icon: click along the bar's right end until its panel opens"
# BT_ICON_X (from the Desktop screenshot) skips the search
BX=""
if [ -n "$BT_ICON_X" ]; then
    $X/x11_probe click $BT_ICON_X 11
    i=0
    while [ $i -lt 120 ] && [ -z "$(panel)" ]; do sleep 5; i=$((i + 5)); done
    [ -n "$(panel)" ] && BX=$BT_ICON_X
    echo "panel after ${i}s"
else
    x=$(( ${GEOM%%x*} - 150 ))
    while [ $x -gt $(( ${GEOM%%x*} - 420 )) ]; do
        $X/x11_probe click $x 11 > /dev/null
        sleep 15   # Naples redraws slowly under TCG
        if [ -n "$(panel)" ]; then BX=$x; break; fi
        x=$((x - 6))
    done
fi
echo "Bluetooth icon at x=$BX, panel $(panel)"; date
$X/x11_probe tree > $T/tree-panel.log 2>&1
if [ -n "$BX" ]; then
    sleep 40   # the scan (10 s) and the redraws under TCG
    shot NaplesBluetooth

    echo "== pair the keyboard (second row under Nearby Devices)"
    g=$(panel); r=${g#*x}; r=${r#*+}; px=${r%%+*}; py=${r#*+}
    # rows from the top: header 40, label 22, Fake Classic 28, Fake Keyboard 28
    $X/x11_probe click $((px + 100)) $((py + 40 + 22 + 28 + 14))
    if wait_window 'Bluetooth Pairing'; then sleep 8; shot NaplesPairing; fi
    i=0
    until /usr/sbin/btsetup status | grep -q "	connected	"; do sleep 3; i=$((i + 3)); [ $i -ge 120 ] && break; done
    /usr/sbin/btsetup status
    sleep 10

    echo "== the panel again"
    [ -n "$(panel)" ] || $X/x11_probe click $BX 11 > /dev/null
    sleep 30
    shot NaplesBluetoothPaired

    echo "== Bluetooth Settings…"
    g=$(panel); h=${g#*x}; h=${h%%+*}; r=${g#*+}; px=${r%%+*}; py=${r#*+}
    $X/x11_probe click $((px + 60)) $((py + h - 8 - 13))
    if wait_window Bluetooth; then sleep $SETTLE; shot SettingsBluetooth; fi
fi

cp $HOME/.xsession-errors $T/xsession-errors.log 2>/dev/null
ls -l $T
sync; sleep 2; sync
echo "SHOTS DONE"
