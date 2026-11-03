#!/bin/bash
# QEMU check of the jetsam (memory-pressure) bands: the system daemons in 19
# (their plists' JetsamProperties, applied by launchd at spawn), the desktop
# session in the default 18, and an application started from the Naples dock
# in 10 (Naples' NSTask -setJetsamPriority:, gnustep-base), as is one opened
# through NSWorkspace (gopen -a Preview, gnustep-gui). Under memory
# pressure the kernel kills the lowest band first, so an application goes
# before the session and the system (seen on the real Pi 2026-11-01 when every
# process was in 18: the daemons, gsdm and the X server were killed first).
# Prints /usr/sbin/jetsam_bands before and after the desktop and the click, then
# runs memhog (band 10) until the kernel kills it and lists who was killed and
# who survived: the session and the daemons must.
# Needs an image injected after build_jetsam_bands.sh, build_memhog.sh and the jetsam changes
# (gnustep-base, gnustep-gui, Naples, the plists), e.g. a fresh tools/make_sd_image.sh image.
# Usage: tools/jetsam_qemu_test.sh [image]
set -e
cd "$(dirname "$0")/.."
export SD_IMAGE="${1:-qemu/darwin_util_shots.img}"
X=/usr/X11/bin
# Activity's dock icon: the second icon of the Naples dock on a 1280x800
# screen (the dock window is 502x68+389+729; Activity at about x 465, y 762).
# Under QEMU, Naples takes longer than its 0.5 s long-click time
# (NaplesDockView.m LongClickMs) to see the button come up, so a click opens
# the icon's menu (seen in a screenshot); its first item, Open, at about
# x 429, y 690, starts the app through the same Naples launch: as a click.
exec tools/boot_test.py --timeout 1800 --login --log qemu/jetsam_boot.log \
    --expect 'JETSAM-TEST DONE' --expect-fail 'panic' \
    --cmd 'echo "== bands after boot"; /usr/sbin/jetsam_bands' \
    --cmd "$X/Xvfb :0 -screen 0 1280x800x24 -nolisten tcp > /tmp/xvfb.log 2>&1 &" \
    --cmd 'sleep 10; DISPLAY=:0 HOME=/var/root sh /etc/gsdm/Xsession > /tmp/xsession.log 2>&1 &' \
    --cmd 'sleep 120; echo "== top-level windows"; DISPLAY=:0 /usr/X11/bin/x11_probe tree | grep -E "^  0x" | grep viewable; DISPLAY=:0 /usr/X11/bin/xwd -root -out /var/root/jetsam_dock.xwd; sync' \
    --cmd 'echo "== click Activity in the dock"; DISPLAY=:0 /usr/X11/bin/xtest_type -c 465 762; sleep 10; DISPLAY=:0 /usr/X11/bin/xtest_type -c 429 690; sleep 45; ps -ax -o pid,command | grep "[A]ctivity.app" || { echo "no Activity after the click; starting it by hand:"; DISPLAY=:0 HOME=/var/root /Applications/Activity.app/Activity > /tmp/act.log 2>&1 & sleep 40; ps -ax -o pid,command | grep "[A]ctivity.app"; tail -5 /tmp/act.log; }' \
    --cmd 'sleep 60; echo "== open Preview through NSWorkspace (gopen)"; DISPLAY=:0 HOME=/var/root /usr/local/bin/gopen -a Preview > /tmp/gopen.log 2>&1; echo "gopen rc=$?"; cat /tmp/gopen.log' \
    --cmd 'sleep 60; ps -ax -o pid,command | grep -E "[A]ctivity.app|[P]review.app|[w]maker|[N]aples|[M]ilan|[X]vfb"; echo "== session log"; tail -25 /var/root/.xsession-errors' \
    --cmd 'echo "== bands with the desktop and Activity"; /usr/sbin/jetsam_bands' \
    --cmd 'echo "== memory pressure: memhog in band 10"; /usr/local/bin/memhog 16 > /tmp/memhog.log 2>&1; echo "memhog exited rc=$?"; tail -2 /tmp/memhog.log' \
    --cmd 'sleep 10; echo "== jetsam kills"; /usr/sbin/kmsg | grep -a "memorystatus: killing"; echo "== survivors"; ps -ax -o pid,command | grep -E "[X]vfb|[w]maker|[N]aples|[M]ilan|[s]yslogd|[c]onfigd|[n]otifyd|[s]shd|[A]ctivity.app|[P]review.app"; echo "JETSAM-TEST DONE"'
