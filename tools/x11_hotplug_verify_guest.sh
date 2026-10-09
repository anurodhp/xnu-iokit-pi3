#!/bin/sh
# DARW-309 guest side, run as root: sh /var/root/x11_hotplug.sh
# Starts real Xorg (puredarwingop + xf86-input-iohid) with NO keyboard or
# mouse attached, and `xev -root`. At each HPM_* marker the host
# (tools/x11_hotplug_qemu_test.sh, boot_test.py --on-match) plugs, unplugs or
# types through the QEMU monitor; this script waits, then counts the events xev
# saw. Counts are printed as RESULT lines for the host to assert on.
export DISPLAY=:0 HOME=/var/root
X=/usr/X11/bin
rm -f /tmp/xev.log
$X/Xorg :0 -logfile /tmp/xorg.log > /tmp/xorg.out 2>&1 &
XP=$!
sleep 30
ps -p $XP > /dev/null 2>&1 || { echo "HPM_XORG_DEAD"; tail -20 /tmp/xorg.log; exit 1; }
$X/xev -root > /tmp/xev.log 2>&1 &
sleep 5
count() { # name
    kp=$(grep -c '^KeyPress' /tmp/xev.log); mo=$(grep -c '^MotionNotify' /tmp/xev.log)
    bp=$(grep -c '^ButtonPress' /tmp/xev.log)
    echo "RESULT $1 keypress=$kp motion=$mo buttonpress=$bp xorg_alive=$(ps -p $XP > /dev/null 2>&1 && echo 1 || echo 0)"
}
echo "== en0 at boot"; ifconfig en0 | grep -E "inet |status"
cpu() { ps -o time= -p $XP; }
count nodevices
echo HPM_A_GO;      sleep 25       # host: device_add usb-kbd + usb-mouse
echo HPM_A_TYPE;    sleep 12       # host: shift-a, mouse move, button 1
count plugged_after_start
echo HPM_B_GO;      sleep 20       # host: device_del both
count unplugged
C1=$(cpu); sleep 20; C2=$(cpu); echo "RESULT idle_cpu_after_unplug before=$C1 after20s=$C2"
ps ax | grep -E "Xorg|xev" | grep -v grep
echo HPM_C_GO;      sleep 25       # host: device_add both again
echo HPM_C_TYPE;    sleep 12       # host: shift-b, mouse move, button 1
count replugged
grep -i "iohid" /tmp/xorg.log | tail -30
echo HPM_END
