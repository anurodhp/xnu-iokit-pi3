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
# x11_probe (installed by inject_into_sd_image.sh) and wmclose_probe (copied by hand to /usr/X11/bin).
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
    env "$@" WM_EVENT_STATS=10 WM_DEFAULTS_TRACE=1 $X/wmaker >> /tmp/wm_$name.log 2>&1 &
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

# chronyd steps the wall clock by seconds on this port (DAR-169): WMRootMenu is only
# rebuilt when its mtime is a newer second than the menu on screen, so a step back
# during the test looks like "menu not reloaded". Stop it (and any respawn) first.
for p in $(ps ax | grep '[c]hronyd' | awk '{print $1}'); do kill -9 $p; done
sleep 2; ps ax | grep -i '[c]hrony'
# reload. The domain files exist when wmaker starts, like on a configured system;
# ABSENT=1 starts without them (the Defaults directory is then created by wmaker).
WARM=40
mkdir -p $D
if [ -z "$ABSENT" ]; then
    printf '{\n}\n' > $D/WindowMaker; printf '{\n}\n' > $D/WMWindowAttributes
    printf '("Base",\n ("Info Panel", INFO_PANEL)\n)\n' > $D/WMRootMenu
fi
startwm r
# mk runs just before a change; stamp gives the changed file an mtime that only ever
# increases (+100 s per change, in the future), whatever the wall clock does.
T=$(( $(date +%s) + 3600 ))
mk() { echo "MARK change $(date +%s)" >> /tmp/wm_r.log; T=$((T + 100)); }
stamp() { touch -t $(date -r $T +%Y%m%d%H%M.%S) "$@"; }
# The root menu stays open from the baseline click on (it is sticky under x11_probe's
# press+release), and MenuTitleBack is repainted on it when WindowMaker is reloaded:
# each snapshot is taken 1 s after the change and shows its title colour.
snap() { sleep 1; cp $F /var/root/vn_$1.xwd; }
wm() { printf '{\n MenuTitleBack = (solid, "%s");\n}\n' "$1"; }
menu() { printf '("%s",\n ("Info Panel", INFO_PANEL),\n ("Refresh", REFRESH)\n)\n' "$1"; }
echo "MARK start $(date +%s)" >> /tmp/wm_r.log
echo "-- baseline: open the root menu"
$X/x11_probe click 600 400 3 > /dev/null; sleep 4; cp $F /var/root/vn_0_base.xwd
echo "-- A: atomic rename-replace of WindowMaker (title red) and WMRootMenu"
mk; menu "ATOMIC-RENAME" > $D/WMRootMenu.tmp; mv $D/WMRootMenu.tmp $D/WMRootMenu; stamp $D/WMRootMenu
wm "#ff0000" > $D/WindowMaker.tmp; mv $D/WindowMaker.tmp $D/WindowMaker; stamp $D/WindowMaker
snap 1_atomic_red
echo "-- B: in-place write (truncate + write) of WindowMaker (title green) and WMRootMenu"
mk; menu "INPLACE-WRITE" > $D/WMRootMenu; stamp $D/WMRootMenu
wm "#00ff00" > $D/WindowMaker; stamp $D/WindowMaker
snap 2_inplace_green
echo "-- C: delete and recreate WindowMaker (title blue) and WMRootMenu"
mk; rm $D/WMRootMenu; menu "DELETE-RECREATE" > $D/WMRootMenu; stamp $D/WMRootMenu
rm $D/WindowMaker; wm "#0000ff" > $D/WindowMaker; stamp $D/WindowMaker
snap 3_recreate_blue
echo "-- D: touch only (mtime bump, content unchanged: expect a reload, no visible change)"
mk; stamp $D/WindowMaker
snap 4_touch
echo "== trace (MARK = when the file was changed, epoch s; defaults-trace = wmaker epoch s.ms)"
grep -E "^MARK|defaults-trace" /tmp/wm_r.log
grep -i "warn\|corrupt\|could not" /tmp/wm_r.log | head -5
stopwm; sync; sync; echo TESTEND
