#!/bin/sh
# DAR-438 guest-side verification, run as root inside the QEMU guest:
#   sh /var/root/wmtest.sh
# Xvfb (real framebuffer via -fbdir) + wmaker + xev + Terminal + Ink, then the
# DAR-424 smoke test (x11_smoketest). Benchmark separately: the GUI apps
# starve it (x11_bench alone: Xvfb + `x11_bench :0 5`).
# Framebuffer snapshots land in /var/root/o2_*.xwd; pull them out of the image
# on the host and convert with tools/xwd_to_png.py. The fb file, not
# `xwd -root`, is the truth for ARGB windows.
export DISPLAY=:0 GNUSTEP_CONFIG_FILE=/etc/GNUstep/GNUstep.conf HOME=/var/root
. /usr/GNUstep/System/Library/Makefiles/GNUstep.sh
rm -rf /var/root/GNUstep/Defaults /var/root/fb; mkdir -p /var/root/fb
mkdir -p /var/root/GNUstep/Library/WindowMaker
printf '#!/bin/sh\nexit 0\n' > /var/root/GNUstep/Library/WindowMaker/autostart; chmod 755 /var/root/GNUstep/Library/WindowMaker/autostart
F=/var/root/fb/Xvfb_screen0
CD=/usr/lib/crashdiag.dylib
/usr/X11/bin/Xvfb :0 -screen 0 1280x720x24 -fbdir /var/root/fb > /tmp/xvfb.log 2>&1 &
sleep 8
/usr/X11/bin/wmaker > /tmp/wm.log 2>&1 &
sleep 25
echo "== smoketest"; /usr/X11/bin/x11_smoketest :0; echo "smoketest rc=$?"
/usr/X11/bin/xev -geometry 300x200+900+40 > /tmp/xev.log 2>&1 &
sleep 10
cp $F /var/root/o2_1_wm_xev.xwd
DYLD_INSERT_LIBRARIES=$CD /Applications/Terminal.app/Terminal > /tmp/term.log 2>&1 &
sleep 60
cp $F /var/root/o2_2_term.xwd
DYLD_INSERT_LIBRARIES=$CD /Applications/Ink.app/Ink > /tmp/ink.log 2>&1 &
sleep 240
cp $F /var/root/o2_3_ink.xwd
echo "== ps"; ps ax | grep -E "Xvfb|wmaker|Terminal|Ink|xev|gdnc|gpbs" | grep -v grep
for l in xvfb wm xev term ink; do echo "== $l.log"; tail -5 /tmp/$l.log | cut -c1-200; done
echo "== probe"; /usr/local/bin/x11_probe tree 2>&1 | grep -v unmapped | grep -v unviewable | head -30
sync; sync
echo TESTEND
