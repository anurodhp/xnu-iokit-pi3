#!/bin/sh
# DAR-434 guest-side verification, run as root inside the QEMU guest or on the Pi:
#   sh /var/root/wmsp.sh unit        spawn_test (attributes, EVFILT_PROC), then its
#                                    launch-latency benchmark with a 0, 64 and 128 MB parent
#   sh /var/root/wmsp.sh e2e [spawn|fork]
#                                    Xvfb + wmaker; menu entries (fds of a child, a sleeper, the dock AutoLaunch, a Relaunch (F9) and a
#                                    kill of the sleeper; reads the WM_SPAWN_TRACE log
#   sh /var/root/wmsp.sh lat [spawn|fork]
#                                    N key presses of F12, each runs `spawn_test --child stamp`;
#                                    launch latency = child's first instruction minus the wSpawn() call
# DISPLAY_NUM / FB for the Pi's scratch Xvfb. Needs on the image: spawn_test, wmaker, xev,
# wmclose_probe, x11_probe, qos_probe (build_*.sh; the first is installed by
# inject_into_sd_image.sh, the others copied by hand to /usr/X11/bin).
export HOME=/var/root
X=/usr/X11/bin
MODE=${1:-unit}
WHICH=${2:-spawn}
N=${N:-20}
DN=${DISPLAY_NUM:-0}
export DISPLAY=:$DN
FB=${FB:-/var/root/fb}
D=$HOME/GNUstep/Defaults

if [ "$MODE" = unit ]; then
    echo "== spawn_test"; $X/spawn_test; echo "rc=$?"
    echo "== bench"; $X/spawn_test bench 30 0; $X/spawn_test bench 30 64; $X/spawn_test bench 30 128
    echo TESTEND; exit 0
fi

rm -rf $D $FB; mkdir -p $FB $D
mkdir -p $HOME/GNUstep/Library/WindowMaker
printf '#!/bin/sh\nexit 0\n' > $HOME/GNUstep/Library/WindowMaker/autostart; chmod 755 $HOME/GNUstep/Library/WindowMaker/autostart
for p in $(ps ax | grep '[c]hronyd' | awk '{print $1}'); do kill -9 $p; done
printf '{\n WindowRelaunchKey = "F9";\n}\n' > $D/WindowMaker
printf '{\n}\n' > $D/WMWindowAttributes
cat > $D/WMRootMenu <<'EOM'
("Spawn test",
 ("Fds", SHORTCUT, "F10", SHEXEC, "/usr/X11/bin/spawn_test --child fds > /tmp/e2e_fds.txt; ls /dev/fd >> /tmp/e2e_fds.txt"),
 ("Sleeper", SHORTCUT, "F11", SHEXEC, "echo $$ > /tmp/e2e_sleeper.pid; exec /bin/sleep 600"),
 ("Stamp", SHORTCUT, "F12", SHEXEC, "/usr/X11/bin/spawn_test --child stamp /tmp/lat_stamps.txt"),
 ("Xev", SHORTCUT, "F8", EXEC, "/usr/X11/bin/xev -geometry 300x200+200+200"),
 ("Missing", SHORTCUT, "F7", EXEC, "/no/such/program")
)
EOM
if [ "$MODE" = e2e ]; then
    cat > $D/WMState <<'EOM'
{
  Dock = {
    Applications = (
      { Command = "/usr/X11/bin/xev -geometry 200x150+700+200"; Name = xev.Xev; AutoLaunch = Yes; Forced = No; Position = "0,0"; Lock = No; },
      { Command = "/no/such/dock/program"; Name = nosuch.Nosuch; AutoLaunch = Yes; Forced = No; Position = "0,1"; Lock = No; }
    );
    Position = "-64,0";
    Lowered = No;
  };
  Clip = { Command = "-"; Name = Logo.WMClip; AutoLaunch = No; Forced = No; Position = "0,0"; };
  Workspaces = ( { Name = "Main"; Clip = { Applications = (); Lowered = Yes; Collapsed = No; AutoAttractIcons = No; KeepAttracted = No; }; } );
}
EOM
fi
# Menu shortcuts (SHORTCUT "F10") only fire with a managed client window under the
# grab (rootmenu.c wRootMenuBindShortcuts), so drive the root menu with the pointer:
# right-click opens it at the pointer, entry N is 20 px high below a 23 px title
# (menu window 116x127 at 543,401 for a click at 600,400, from x11_probe tree).
pick() { $X/x11_probe click 600 400 3 > /dev/null; sleep 1; $X/x11_probe click 600 $((434 + 20 * $1)) 1 > /dev/null; }
rm -f /tmp/e2e_* /tmp/lat_stamps.txt /tmp/wm_sp.log
$X/Xvfb :$DN -screen 0 1280x720x24 -fbdir $FB > /tmp/xvfb_sp.log 2>&1 &
XP=$!
sleep ${XWAIT:-8}
ENVV="WM_SPAWN_TRACE=1 WM_EVENT_STATS=10"
[ "$WHICH" = fork ] && ENVV="$ENVV WM_SPAWN=fork"
env $ENVV $X/wmaker > /tmp/wm_sp.log 2>&1 &
MON=$!
sleep ${WARM:-30}
WP=$(ps ax | grep -e '--for-real' | grep -v grep | awk '{print $1}')
echo "== [$WHICH] xvfb $XP monitor $MON wmaker $WP"
ps -o pid,pri,nice,rss,time,comm -p $XP,$WP

if [ "$MODE" = menu ]; then
    $X/x11_probe click 600 400 3 > /dev/null; sleep 4
    $X/x11_probe tree | grep -v "1x1"
    cp $FB/Xvfb_screen0 $HOME/sp_menu.xwd
    kill $MON; kill $WP; kill $XP; sleep 2; echo TESTEND; exit 0
fi
if [ "$MODE" = lat ]; then
    i=0; while [ $i -lt $N ]; do pick 2; sleep 1; i=$((i + 1)); done
    sleep 3
    echo "== trace lines"; grep "posix_spawnp \|fork+exec " /tmp/wm_sp.log | grep "stamp" | head -3
    grep "stamp" /tmp/wm_sp.log | sed -n 's/.*t0=\([0-9.]*\) call=\([0-9.]*\) ms/\1 \2/p' > /tmp/lat_t0.txt
    paste /tmp/lat_t0.txt /tmp/lat_stamps.txt | awk 'NF==3 { d=$3-$1; c=$2; n++; sd+=d; sc+=c; if (n==1||d<mn) mn=d; if (d>mx) mx=d; printf "launch %d: call %.2f ms, child started %.2f ms after the call\n", n, c, d }
        END { if (n) printf "LAT %d launches: call avg %.2f ms; start-to-child avg %.2f ms (min %.2f max %.2f)\n", n, sc/n, sd/n, mn, mx }'
    echo "== wmaker"; ps -o pid,pri,rss,time -p $WP
    kill $MON; kill $WP; kill $XP; sleep 2; echo TESTEND; exit 0
fi

# ---- e2e ----
echo "== dock autolaunch (execCommand in dock.c)"
grep "xev\|nosuch\|no/such" /tmp/wm_sp.log | head
echo "== wmaker windows"; $X/wmclose_probe list
echo "== F10: a menu launch; the child's descriptors"
pick 0; sleep 3
ls -l /tmp/e2e_* 2>&1; cat /tmp/e2e_fds.txt
echo "== F11: a long-running child; its pid, ps"
pick 1; sleep 3
SP=$(cat /tmp/e2e_sleeper.pid); echo "sleeper pid $SP"
ps -o pid,ppid,pgid,sess,pri,nice,state,comm -p $SP,$WP
echo "-- scheduling (qos_probe: thread base/ext priorities); children must not inherit wmaker's boost"
echo "wmaker:"; $X/qos_probe pid $WP | grep "ext base"
echo "sleeper (spawned by wmaker):"; $X/qos_probe pid $SP | grep "ext base"
echo "-- kill it"
kill $SP; sleep 3
ps -o pid,state,comm -p $SP 2>&1 | tail -1
grep "pid $SP " /tmp/wm_sp.log
echo "== kill the dock-launched xev (pid $(ps ax -o pid,comm | awk '/[x]ev$/ {print $1; exit}'))"
DX=$(ps ax -o pid,comm | awk '/[x]ev$/ {print $1; exit}'); kill $DX; sleep 3
grep "pid $DX " /tmp/wm_sp.log
echo "== ps for zombies"; ps ax -o pid,ppid,state,comm | awk '$3 ~ /Z/'
echo "== F7: launching a program that does not exist"
pick 4; sleep 2
grep -i "no/such\|could not exec" /tmp/wm_sp.log | head -3
echo "== Relaunch: focus an xev window, press F9 (WindowRelaunchKey)"
$X/wmclose_probe list | grep -i xev
pick 3; sleep 4
$X/wmclose_probe list | grep -i xev
XW=$($X/wmclose_probe list | grep -i xev | tail -1 | awk '{print $1}')
$X/wmclose_probe titlebar xev > /dev/null; sleep 1
$X/wmclose_probe key F9; sleep 4
echo "-- xev windows after F9:"; $X/wmclose_probe list | grep -i xev
ps ax -o pid,ppid,pgid,pri,comm | grep "[x]ev"
echo "== trace (spawns and deaths)"
grep "wmaker spawn" /tmp/wm_sp.log
echo "== wmaker stats / warnings"
grep -i "warn\|error\|fatal" /tmp/wm_sp.log | head -5
echo "== wmaker ps"; ps -o pid,pri,rss,time -p $WP
cp $FB/Xvfb_screen0 $HOME/sp_final.xwd 2>/dev/null
kill $MON; kill $WP; kill $XP; sleep 3; sync
echo TESTEND
