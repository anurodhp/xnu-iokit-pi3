#!/bin/sh
# DAR-433 guest-side verification, run as root inside the QEMU guest:
#   sh /var/root/wmvn.sh unit     wutil_vnode_test on /tmp and /var/root (HFS+ root),
#                                 then the DAR-431/432 regression tests
#   sh /var/root/wmvn.sh idle     Xvfb + wmaker (a discarded warm-up, then two runs):
#                                 idle CPU and WM_EVENT_STATS wait counters over IDLE s
#   sh /var/root/wmvn.sh reload   Xvfb + wmaker; change WMRootMenu / WindowMaker by
#                                 atomic rename, in-place write and delete+recreate;
#                                 open the root menu 1 s after each change, snapshot
#                                 to /var/root/vn_*.xwd (pull out of the image on the
#                                 host, tools/xwd_to_png.py)
# Needs on the image: wutil_vnode_test, wutil_kq_test, wutil_timer_test, wmlat,
# x11_probe, wmclose_probe (installed by inject_into_sd_image.sh).
export DISPLAY=:0 HOME=/var/root
X=/usr/X11/bin
IDLE=${IDLE:-60}
MODE=${1:-unit}
D=/var/root/GNUstep/Defaults
F=/var/root/fb/Xvfb_screen0

if [ "$MODE" = unit ]; then
    echo "== vnode: default backend"; $X/wutil_vnode_test /tmp /var/root; echo "rc=$?"
    echo "== vnode: select (W_KQueueAddFilter returns NULL, expect FAIL)"
    WM_EVENT_BACKEND=select $X/wutil_vnode_test /tmp | tail -2
    echo "== regression: wutil_kq_test, wutil_timer_test"
    $X/wutil_kq_test | grep -v "^PASS"; echo "rc=$?"
    WM_EVENT_BACKEND=select $X/wutil_kq_test | grep -v "^PASS"; echo "rc=$?"
    $X/wutil_timer_test; echo "rc=$?"
    echo TESTEND; exit 0
fi

rm -rf /var/root/GNUstep/Defaults /var/root/fb; mkdir -p /var/root/fb
mkdir -p /var/root/GNUstep/Library/WindowMaker
printf '#!/bin/sh\nexit 0\n' > /var/root/GNUstep/Library/WindowMaker/autostart; chmod 755 /var/root/GNUstep/Library/WindowMaker/autostart
$X/Xvfb :0 -screen 0 1280x720x24 -fbdir /var/root/fb > /tmp/xvfb.log 2>&1 &
sleep 8

startwm() { # name [env...]
    name=$1; shift
    : > /tmp/wm_$name.log
    env "$@" WM_EVENT_STATS=10 $X/wmaker >> /tmp/wm_$name.log 2>&1 &
    MON=$!
    sleep ${WARM:-25}
    WP=$(ps ax | grep -e '--for-real' | grep -v grep | awk '{print $1}')
    echo "== [$name] monitor $MON wmaker $WP"
}
stopwm() {
    kill $MON; kill $WP
    i=0; while ps -p $WP > /dev/null 2>&1 && [ $i -lt 90 ]; do sleep 1; i=$((i + 1)); done
    echo "wmaker gone after ${i}s"
}

if [ "$MODE" = idle ]; then
    for name in warm m1 m2; do
        startwm $name
        echo "MARK idle-start" >> /tmp/wm_$name.log
        t0=$(ps -p $WP -o time=)
        sleep $IDLE
        echo "MARK idle-end" >> /tmp/wm_$name.log
        t1=$(ps -p $WP -o time=)
        echo "[$name] idle TIME before=$t0 after=$t1 over ${IDLE}s"
        echo "[$name] stats lines in the idle window:"
        sed -n '/idle-start/,/idle-end/p' /tmp/wm_$name.log | grep -E "event stats|timer callback" | cut -c1-200
        [ $name = m2 ] || stopwm
    done
    echo "== stat() calls: none observable here; stats above count waits and timer callbacks"
    stopwm; sync; sync; echo TESTEND; exit 0
fi

# reload
WARM=40
startwm r
mkdir -p $D
openmenu() { $X/x11_probe click 600 400 3 > /dev/null; sleep 4; cp $F /var/root/vn_$1.xwd; $X/wmclose_probe key Escape > /dev/null; $X/x11_probe click 900 650 1 > /dev/null; sleep 2; }
menu() { # title colour
    printf '("%s",\n ("Info Panel", INFO_PANEL),\n ("Refresh", REFRESH)\n)\n' "$1"
}
echo "-- baseline"; openmenu 0_base
echo "-- A: atomic rename-replace of WMRootMenu + WindowMaker (MenuTitleBack red), menu opened ~1 s later"
menu "ATOMIC-RENAME" > $D/WMRootMenu.tmp; mv $D/WMRootMenu.tmp $D/WMRootMenu
printf '{\n MenuTitleBack = (solid, "#ff0000");\n}\n' > $D/WindowMaker.tmp; mv $D/WindowMaker.tmp $D/WindowMaker
date; openmenu 1_atomic
echo "-- B: in-place write (truncate + write) of WMRootMenu"
menu "INPLACE-WRITE" > $D/WMRootMenu
date; openmenu 2_inplace
echo "-- C: delete and recreate WMRootMenu"
rm $D/WMRootMenu; menu "DELETE-RECREATE" > $D/WMRootMenu
date; openmenu 3_recreate
echo "-- D: WindowMaker back to blue by touch-free rewrite (atomic)"
printf '{\n MenuTitleBack = (solid, "#0000ff");\n}\n' > $D/WindowMaker.tmp; mv $D/WindowMaker.tmp $D/WindowMaker
date; openmenu 4_blue
grep -c . /tmp/wm_r.log; grep -i "warn\|corrupt\|could not" /tmp/wm_r.log | head -5
stopwm; sync; sync; echo TESTEND
