#!/bin/sh
# DAR-432 guest-side verification, run as root inside the QEMU guest:
#   sh /var/root/wmtm.sh unit     kevent64 timer probe, then wutil_timer_test on the
#                                 default (kqueue) then the select backend
#   sh /var/root/wmtm.sh idle     Xvfb + one wmaker: idle CPU and WM_EVENT_STATS
#                                 counters over IDLE seconds (2 windows), then a
#                                 -3600 s / +3600 s wall-clock step under the
#                                 running wmaker with per-window timer counts
# Needs on the image: wutil_timer_test, wmlat (installed by
# inject_into_sd_image.sh). Same conventions as wm_kqueue_verify_guest.sh.
export DISPLAY=:0 HOME=/var/root
X=/usr/X11/bin
IDLE=${IDLE:-60}
MODE=${1:-unit}

if [ "$MODE" = unit ]; then
    echo "== raw kevent64 EVFILT_TIMER probe"; $X/wutil_timer_test kq64
    echo "== unit: default backend"; $X/wutil_timer_test; echo "rc=$?"
    echo "== unit: select"; WM_EVENT_BACKEND=select $X/wutil_timer_test; echo "rc=$?"
    echo "== regression: wutil_kq_test (DAR-431) default, select"; $X/wutil_kq_test | grep -v "^PASS"; echo "rc=$?"
    WM_EVENT_BACKEND=select $X/wutil_kq_test | grep -v "^PASS"; echo "rc=$?"
    echo TESTEND; exit 0
fi

rm -rf /var/root/GNUstep/Defaults /var/root/fb; mkdir -p /var/root/fb
mkdir -p /var/root/GNUstep/Library/WindowMaker
printf '#!/bin/sh\nexit 0\n' > /var/root/GNUstep/Library/WindowMaker/autostart; chmod 755 /var/root/GNUstep/Library/WindowMaker/autostart
$X/Xvfb :0 -screen 0 1280x720x24 -fbdir /var/root/fb > /tmp/xvfb.log 2>&1 &
sleep 8

run() { # name
    name=$1
    : > /tmp/wm_$name.log
    env WM_EVENT_STATS=10 $X/wmaker >> /tmp/wm_$name.log 2>&1 &
    MON=$!
    sleep 25
    WP=$(ps ax | grep -e '--for-real' | grep -v grep | awk '{print $1}')
    echo "== [$name] monitor $MON wmaker $WP"
    echo "MARK idle-start" >> /tmp/wm_$name.log
    t0=$(ps -p $WP -o time=)
    sleep $IDLE
    echo "MARK idle-end" >> /tmp/wm_$name.log
    t1=$(ps -p $WP -o time=)
    echo "[$name] idle TIME before=$t0 after=$t1 over ${IDLE}s"
    $X/wmlat rt 200
    echo "[$name] stats lines in the idle window:"
    sed -n '/idle-start/,/idle-end/p' /tmp/wm_$name.log | grep -E "event stats|timer callback" | cut -c1-200
}
stopwm() {
    kill $MON; kill $WP
    i=0; while ps -p $WP > /dev/null 2>&1 && [ $i -lt 90 ]; do sleep 1; i=$((i + 1)); done
    echo "wmaker gone after ${i}s"
}
# the first wmaker after a fresh Xvfb is ~25x slower to answer: discard one
run warm; stopwm
run m1; stopwm
run m2
echo "== clock step under the running wmaker (m2): -3600 s"
echo "MARK step-back" >> /tmp/wm_m2.log
$X/wutil_timer_test step -3600; date
sleep 30
echo "MARK step-fwd" >> /tmp/wm_m2.log
$X/wutil_timer_test step 3600; date
sleep 25
echo "MARK end" >> /tmp/wm_m2.log
sed -n '/step-back/,$p' /tmp/wm_m2.log | grep -E "MARK|event stats|timer callback" | cut -c1-200
ps -p $WP -o pid,state,time
stopwm
tail -6 /tmp/wm_m2.log | cut -c1-200
sync; sync
echo TESTEND
