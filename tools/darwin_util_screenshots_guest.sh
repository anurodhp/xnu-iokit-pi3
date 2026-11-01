#!/bin/sh
# Guest side of tools/darwin_util_screenshots_qemu.sh (copied to
# /var/root/darwin_util_shots.sh and run as root in the QEMU guest). It brings
# up the real desktop on Xvfb -- gsdm's Xsession as shipped, so Window Maker
# and its autostart (Naples' dock and menu bar, Milan's desktop) -- then opens
# each darwin-util app in turn and takes a screenshot of the whole screen
# (xwd) into /var/root/shots/<App>.xwd. QEMU has no HDMI framebuffer, hence
# Xvfb; everything else is what the Pi runs.
#   Console   on the kernel message buffer (root)
#   Activity  after enough samples for the CPU graph to have a history
#   Preview   /var/root/shots/Lake.png with a selection dragged across it
# GNUstep starts slowly under QEMU/TCG (minutes per app): every step waits for
# its window to be mapped (x11_probe tree) and then lets it finish drawing.
T=/var/root/shots
X=/usr/X11/bin
GEOM=${GEOM:-1280x800}
WAIT=${WAIT:-900}
SETTLE=${SETTLE:-45}
export DISPLAY=:0 HOME=/var/root

echo "== Xvfb $GEOM"
$X/Xvfb :0 -screen 0 ${GEOM}x24 -nolisten tcp > $T/xvfb.log 2>&1 &
i=0
until $X/x11_probe tree > /dev/null 2>&1; do
    sleep 2; i=$((i + 2))
    [ $i -ge 120 ] && { echo "Xvfb did not start"; cat $T/xvfb.log; echo "SHOTS DONE"; exit 1; }
done

# wait_window <pattern>: until a viewable window whose name matches is mapped
wait_window() {
    i=0
    while [ $i -lt $WAIT ]; do
        $X/x11_probe tree 2>/dev/null | grep ' viewable' | grep -q "name=$1" && break
        sleep 5; i=$((i + 5))
    done
    echo "window '$1' after ${i}s"
    [ $i -lt $WAIT ]
}
shot() { $X/xwd -root -out $T/$1.xwd; echo "shot $1 rc=$?"; }

echo "== desktop (gsdm's Xsession: Window Maker, Naples, Milan)"
sh /etc/gsdm/Xsession > $T/xsession.log 2>&1 &
# Naples' bar and dock windows are untitled, so wait for the processes, then
# give both time to draw (minutes under TCG)
i=0
until ps -ax -o command | grep -q '^/Applications/Milan.app/Milan'; do
    sleep 5; i=$((i + 5))
    [ $i -ge 300 ] && { echo "Milan did not start"; break; }
done
ps -ax -o pid,command | grep -E '[w]maker|[N]aples|[M]ilan'
sleep ${DESKTOP_SETTLE:-180}
# the GNUstep environment the Xsession sets, for the apps started below
export GNUSTEP_CONFIG_FILE=/etc/GNUstep/GNUstep.conf
[ -f /System/Library/Makefiles/GNUstep.sh ] && . /System/Library/Makefiles/GNUstep.sh

echo "== Console"
/Applications/Console.app/Console -ConsoleSource Kernel > $T/console.log 2>&1 &
CP=$!
wait_window Console && sleep $SETTLE && shot Console
kill $CP 2>/dev/null; sleep 5

echo "== Activity"
/Applications/Activity.app/Activity -ActivityInterval 2 -ActivityFilter 0 \
    -ActivitySortKey cpu -ActivitySortAscending NO > $T/activity.log 2>&1 &
AP=$!
# a minute and a half of samples, so the graph has a history
wait_window Activity && sleep 90 && shot Activity
kill $AP 2>/dev/null; sleep 5

echo "== Preview"
/Applications/Preview.app/Preview $T/Lake.png > $T/preview.log 2>&1 &
PP=$!
if wait_window Lake.png; then
    sleep $SETTLE
    # drag a selection across the middle of the window's content: the window's
    # root-relative geometry from x11_probe's "WxH+X+Y" for the Lake.png window
    g=$($X/x11_probe tree | grep ' viewable' | grep 'name=Lake.png' | head -1 | awk '{print $2}')
    echo "Preview window $g"
    w=${g%%x*}; r=${g#*x}; h=${r%%+*}; r=${r#*+}; x=${r%%+*}; y=${r#*+}
    $X/x11_probe drag $((x + w * 30 / 100)) $((y + h * 28 / 100)) $((x + w * 78 / 100)) $((y + h * 80 / 100))
    sleep 20
    shot Preview
fi
kill $PP 2>/dev/null; sleep 5

ls -l $T
# boot_test stops QEMU as soon as it sees SHOTS DONE: flush the last
# screenshot to the disk image first (once it was listed here but missing
# on the host)
sync; sleep 2; sync
echo "SHOTS DONE"
