#!/bin/sh
# DAR-431 guest-side verification, run as root inside the QEMU guest:
#   sh /var/root/wmkq.sh            everything
#   sh /var/root/wmkq.sh quick      unit test + select/kqueue A/B only (no GNUstep apps)
# Needs on the image: wutil_kq_test, wmlat, x11_probe, xtest_type (installed
# by inject_into_sd_image.sh) and wmclose_probe (copied by hand).
#  1. wutil_kq_test under both backends (WM_EVENT_BACKEND=select|default).
#  2. Xvfb, then Window Maker once per backend on the same server:
#     idle CPU/state (ps TIME delta over IDLE seconds), WM_EVENT_STATS wait
#     counters over the same window, wmlat rt/burst latency.
#  3. (full) under the kqueue wmaker: root menu, window open/close, xev key
#     and pointer input, Terminal and Ink launch; fb snapshots in /var/root.
export DISPLAY=:0 HOME=/var/root
X=/usr/X11/bin
IDLE=${IDLE:-60}
MODE=${1:-full}
# func: only the functional part, on one kqueue wmaker
[ "$MODE" = func ] && { ORDER=kqueue; IDLE=5; }
rm -rf /var/root/GNUstep/Defaults /var/root/fb; mkdir -p /var/root/fb
mkdir -p /var/root/GNUstep/Library/WindowMaker
printf '#!/bin/sh\nexit 0\n' > /var/root/GNUstep/Library/WindowMaker/autostart; chmod 755 /var/root/GNUstep/Library/WindowMaker/autostart
F=/var/root/fb/Xvfb_screen0

if [ "$MODE" != menu ] && [ "$MODE" != func ]; then
echo "== unit: kqueue"; $X/wutil_kq_test; echo "rc=$?"
echo "== unit: select"; WM_EVENT_BACKEND=select $X/wutil_kq_test; echo "rc=$?"
fi

$X/Xvfb :0 -screen 0 1280x720x24 -fbdir /var/root/fb > /tmp/xvfb.log 2>&1 &
sleep 8

ab() { # backend-name  env-setting
    name=$1; shift
    : > /tmp/wm_$name.log
    env "$@" WM_EVENT_STATS=10 $X/wmaker >> /tmp/wm_$name.log 2>&1 &
    MON=$!      # wmaker's monitor process (src/monitor.c); the real WM is its --for-real child
    sleep 25
    WP=$(ps ax | grep -e '--for-real' | grep -v grep | awk '{print $1}')
    echo "== [$name] monitor pid $MON, wmaker pid $WP"; ps -p $WP -o pid,state,time,command
    echo "MARK idle-start" >> /tmp/wm_$name.log
    ps -p $WP -o time= > /tmp/t0_$name
    sleep $IDLE
    echo "MARK idle-end" >> /tmp/wm_$name.log
    ps -p $WP -o time= > /tmp/t1_$name
    echo "[$name] idle TIME before=$(cat /tmp/t0_$name) after=$(cat /tmp/t1_$name) over ${IDLE}s"
    ps -p $WP -o pid,state,time,command
    echo "[$name] latency:"
    $X/wmlat rt 200
    $X/wmlat rt 200
    $X/wmlat burst 200
    $X/wmlat burst 200
    echo "[$name] stats (idle window = lines between the MARKs):"; grep -E "MARK|event stats" /tmp/wm_$name.log | sed -n '/idle-start/,/idle-end/p'; grep -B1 -m1 "idle-start" /tmp/wm_$name.log | head -2
    echo "[$name] ps after load:"; ps -p $WP -o pid,state,time
}
# wmaker takes several seconds to exit under TCG; a still-running one makes the
# next start fail with "already a window manager running".
stopwm() {
    kill $MON; kill $WP
    i=0; while ps -p $WP > /dev/null 2>&1 && [ $i -lt 90 ]; do sleep 1; i=$((i + 1)); done
    echo "wmaker gone after ${i}s"; ps ax | grep -e wmaker | grep -v grep
}
# menu: per backend, open the root menu (button 3), snapshot, click outside
# it (button 1), snapshot, to compare menu dismissal between the backends.
if [ "$MODE" = menu ]; then
    for b in select kqueue; do
        : > /tmp/wm_menu_$b.log
        env WM_EVENT_BACKEND=$b $X/wmaker >> /tmp/wm_menu_$b.log 2>&1 &
        MON=$!; sleep 40
        WP=$(ps ax | grep -e '--for-real' | grep -v grep | awk '{print $1}')
        $X/x11_probe click 600 400 3; sleep 5; cp $F /var/root/menu_${b}_a_open.xwd
        $X/x11_probe click 900 300 1; sleep 5; cp $F /var/root/menu_${b}_b_after_click.xwd
        $X/x11_probe click 600 400 3; sleep 5; $X/wmclose_probe key Escape; sleep 5
        cp $F /var/root/menu_${b}_c_after_escape.xwd
        kill $MON; kill $WP; i=0; while ps -p $WP > /dev/null 2>&1 && [ $i -lt 90 ]; do sleep 1; i=$((i + 1)); done
    done
    sync; sync; echo TESTEND; exit 0
fi

# The first wmaker after a fresh Xvfb is ~25x slower to answer (median
# 275 ms vs 10 ms per round trip, whichever backend it runs), so the first
# run is a discarded "warm" one and the backends then alternate.
# ORDER overrides it. The last entry's wmaker is left running for the
# functional part, so full mode needs ORDER to end in kqueue.
ORDER=${ORDER:-warm select kqueue select kqueue}
LAST=${ORDER##* }
for b in $ORDER; do
    n=$((${n:-0} + 1)); ab $b.$n WM_EVENT_BACKEND=${b#warm}
    [ $n -eq $(echo $ORDER | wc -w) ] && break
    stopwm; echo "[$b.$n] exit stats:"; tail -1 /tmp/wm_$b.$n.log
done
[ "$MODE" = quick ] && { stopwm; tail -1 /tmp/wm_$LAST.$n.log; echo TESTEND; exit 0; }

echo "== functional (kqueue wmaker, pid $WP)"
$X/x11_probe tree 2>&1 | grep -v unmapped | grep -v unviewable | head -10
$X/xev -geometry 300x200+900+40 > /tmp/xev.log 2>&1 &
XEVP=$!
sleep 10
$X/wmclose_probe list
echo "-- root menu (button 3 click), snapshot"
$X/x11_probe click 600 400 3
sleep 4
cp $F /var/root/kq_1_menu.xwd
$X/wmclose_probe key Escape; $X/x11_probe click 700 600 1
sleep 2
echo "-- keyboard into xev"
$X/x11_probe click 1000 140 1; sleep 1
$X/xtest_type hello; sleep 3
grep -c KeyPress /tmp/xev.log; grep -m3 "keysym" /tmp/xev.log | cut -c1-100
echo "-- pointer into xev (ButtonPress/MotionNotify lines)"; grep -c "MotionNotify\|ButtonPress" /tmp/xev.log
echo "-- wmclose delete xev"
$X/wmclose_probe delete Event; sleep 5
$X/wmclose_probe list | grep -ci "Event Tester"
kill $XEVP 2>/dev/null
echo "-- Terminal"
. /usr/GNUstep/System/Library/Makefiles/GNUstep.sh 2>/dev/null
export GNUSTEP_CONFIG_FILE=/etc/GNUstep/GNUstep.conf
CD=/usr/lib/crashdiag.dylib
DYLD_INSERT_LIBRARIES=$CD /usr/GNUstep/System/Applications/Terminal.app/Terminal > /tmp/term.log 2>&1 &
sleep 60
cp $F /var/root/kq_2_term.xwd
$X/wmclose_probe list
DYLD_INSERT_LIBRARIES=$CD /usr/GNUstep/System/Applications/Ink.app/Ink > /tmp/ink.log 2>&1 &
sleep 120
cp $F /var/root/kq_3_ink.xwd
$X/wmclose_probe list
echo "-- close the Ink document window via its titlebar close button"
$X/wmclose_probe closebtn Untitled; sleep 8
$X/wmclose_probe list
ps -p $WP -o pid,state,time,command
ps ax | grep -E "Xvfb|wmaker|Terminal|Ink" | grep -v grep
grep -E "event stats" /tmp/wm_$LAST.$n.log | tail -2
stopwm; tail -1 /tmp/wm_$LAST.$n.log
sync; sync
echo TESTEND
