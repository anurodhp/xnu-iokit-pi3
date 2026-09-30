#!/bin/sh
# DAR-439 guest-side verification: runs as a launchd job
# (org.puredarwin.qostest.plist, ProcessType=Interactive, the same as the real
# X session job) so every child has the session's proctype. Output goes to
# /var/log/qostest.log. Mode comes from /var/root/qos_mode:
#   base   Xvfb + wmaker started unmodified
#   x / w / xw   Xvfb / wmaker / both boosted with DYLD_INSERT_LIBRARIES=
#          /usr/lib/qos_boost.dylib (constructor calls pthread_set_qos_class_
#          self_np(USER_INTERACTIVE) on the main thread; stands in for the
#          Xorg-side call). A DAR-439 wmaker does it itself: run it in base/x.
# Phases: priority dump (ps + per-thread thread_info), then wmlat round trips
# idle, with `yes` competing at default, and with `yes` at nice 20, on ONE core.
export DISPLAY=:0 HOME=/var/root
X=/usr/X11/bin
MODE=$(cat /var/root/qos_mode 2>/dev/null); MODE=${MODE:-base}
WM=${WM:-wmaker}
N=${N:-200}
LOG=/var/log/qostest.log
exec > $LOG 2>&1
echo "== qostest mode=$MODE wm=$WM"
rm -rf /var/root/GNUstep/Defaults /var/root/fb; mkdir -p /var/root/fb
mkdir -p /var/root/GNUstep/Library/WindowMaker
printf '#!/bin/sh\nexit 0\n' > /var/root/GNUstep/Library/WindowMaker/autostart; chmod 755 /var/root/GNUstep/Library/WindowMaker/autostart
if [ $MODE = xorg ]; then
    # real Xorg with the DAR-439 puredarwingop driver: no framebuffer on plain
    # QEMU, so it dies after the module load; sample the main thread while it lives
    $X/Xorg :1 -logfile /tmp/xorg.log > /tmp/xorg.out 2>&1 &
    XP=$!
    i=0
    while [ $i -lt 60 ]; do
        sleep 1; i=$((i + 1))
        ps -p $XP > /dev/null 2>&1 || { echo "Xorg gone after ${i}s"; break; }
        $X/qos_probe pid $XP | grep "ext base" | sed "s/^/t=${i}s /"
    done
    grep -i "puredarwingop\|Module\|Fatal\|error" /tmp/xorg.log | head -20
    echo QOSEND; sync; exit 0
fi
echo "== job shell"; $X/qos_probe self set
case $MODE in *x*) XB=/usr/lib/qos_boost.dylib;; *) XB=;; esac
case $MODE in *w*) WB=/usr/lib/qos_boost.dylib;; *) WB=;; esac
xv() { if [ -n "$1" ]; then b=$1; shift; env DYLD_INSERT_LIBRARIES=$b "$@"; else shift; "$@"; fi; }
xv "$XB" $X/Xvfb :0 -screen 0 1280x720x24 -fbdir /var/root/fb > /tmp/xvfb.log 2>&1 &
sleep 10
startwm() {
    xv "$WB" $X/$WM >> /tmp/wm.log 2>&1 &
    MON=$!
    sleep 30
    WP=$(ps ax | grep -e '--for-real' | grep -v grep | awk '{print $1}')
}
stopwm() {
    kill $MON; kill $WP
    i=0; while ps -p $WP > /dev/null 2>&1 && [ $i -lt 90 ]; do sleep 1; i=$((i + 1)); done
}
startwm; stopwm      # discard the slow first wmaker after a fresh Xvfb
startwm
XP=$(ps ax | grep "Xvfb" | grep -v grep | grep -v qos_probe | awk '{print $1}' | head -1)
echo "== pids: Xvfb=$XP wmaker=$WP monitor=$MON"
echo "== ps"; ps -o pid,pri,nice,comm -p $XP,$WP,$MON,$$
echo "== threads Xvfb"; $X/qos_probe pid $XP
echo "== threads wmaker"; $X/qos_probe pid $WP
echo "== self (client) and slop"; $X/qos_probe self; $X/qos_probe slop; $X/qos_probe slop set; $X/qos_probe slop; $X/qos_probe slop set
echo "== wmlat idle"; $X/wmlat rt $N
# load: name, count, how to start one
YES=$(command -v yes); YES=${YES:-/usr/bin/yes}
load() { # name count "command"
    name=$1; cnt=$2; cmd=$3; pids=; plist=
    i=0; while [ $i -lt $cnt ]; do $cmd > /dev/null & pids="$pids $!"; plist="$plist,$!"; i=$((i + 1)); done
    sleep 4
    echo "== load $name ($cnt x yes)"; ps -o pid,pri,nice,comm -p ${plist#,}
    $X/wmlat rt $N
    kill $pids; wait $pids 2>/dev/null
}
load "yes x1 default" 1 "$YES"
load "yes x4 default" 4 "$YES"
load "yes x1 nice20" 1 "nice -n 20 $YES"
load "yes x1 at USER_INTERACTIVE (qos_boost)" 1 "env DYLD_INSERT_LIBRARIES=/usr/lib/qos_boost.dylib $YES"
echo "== ps after"; ps -o pid,pri,nice,comm -p $XP,$WP
echo QOSEND
sync
