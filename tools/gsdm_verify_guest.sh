#!/bin/sh
# gsdm guest-side check, run as root inside the QEMU guest by
# tools/gsdm_qemu_test.sh (which copies it to /var/root/gsdm_verify.sh).
# gsdm runs with its real greeter and real Xsession (Window Maker) on
# Xvfb :0 instead of Xorg (QEMU has no HDMI framebuffer); everything else
# is /etc/gsdm as shipped. Screenshots (xwd) land in /var/root/gsdmtest.
#  1. a test account alice (uid 501, password "wonderland");
#  2. the login window: user list;
#  3. click alice, wrong password -> "Incorrect password." and no login;
#  4. right password -> Xsession runs as alice (wmaker), ~alice/.Xauthority;
#  5. logout (wmaker exits) -> gsdm terminates the server and shows the
#     login window on a fresh one;
#  6. SIGTERM gsdm -> X server and greeter gone, gsdm exits 0.
T=/var/root/gsdmtest
X=/usr/X11/bin
W=${GREETER_WAIT:-600}
rm -rf $T; mkdir -p $T

echo "== account"
grep -q '^alice:' /etc/master.passwd ||
    /usr/sbin/pw useradd alice -u 501 -m -s /bin/sh -c "Alice Liddell"
# pw usermod -h cannot hash here ("setting crypt(3) format" fails), so
# gsdm_qemu_test.sh hashes "wonderland" on the host (hash_password_host.c,
# the hasher that provisions root's password) and this writes it in.
awk -F: -v OFS=: -v h="$(cat /var/root/gsdm_alice.hash)" '$1 == "alice" { $2 = h } 1' \
    /etc/master.passwd > $T/master.passwd && cat $T/master.passwd > /etc/master.passwd
grep '^alice:' /etc/master.passwd | cut -d: -f1,3,4,8,9,10
grep -c '^alice:_' /etc/master.passwd | sed 's/^/alice has a DES hash: /'

cat > $T/gsdm.conf <<EOF
display = :0
server = $X/Xvfb :0 -screen 0 1024x768x24 -nolisten tcp
auth_dir = /var/lib/gsdm
greeter = $T/Xgreeter
greeter_home = /var/lib/gsdm/greeter
session = /etc/gsdm/Xsession
user_path = /usr/X11/bin:/usr/local/bin:/bin:/usr/bin
system_path = /usr/X11/bin:/usr/local/sbin:/usr/local/bin:/sbin:/usr/sbin:/bin:/usr/bin
min_uid = 500
title = Darwin
EOF
# The shipped Xgreeter, plus the crash reporter (DARW-301) when the image has it.
echo "== password check (gsdm -check-password: the login path without X)"
echo wonderland | /usr/local/sbin/gsdm -config $T/gsdm.conf -check-password alice
echo nottherightone | /usr/local/sbin/gsdm -config $T/gsdm.conf -check-password alice
echo darwin | /usr/local/sbin/gsdm -config $T/gsdm.conf -check-password root

cat > $T/Xgreeter <<'EOF'
#!/bin/sh
[ -f /usr/lib/crashdiag.dylib ] && export DYLD_INSERT_LIBRARIES=/usr/lib/crashdiag.dylib
exec /etc/gsdm/Xgreeter
EOF
chmod 755 $T/Xgreeter
/usr/local/sbin/gsdm -config $T/gsdm.conf > $T/gsdm.log 2>&1 &
GP=$!
export DISPLAY=:0 XAUTHORITY=/var/lib/gsdm/auth-0
shot() { $X/xwd -root -out $T/$1.xwd; echo "shot $1 rc=$?"; }
greeter_pids() { ps -ax -o pid,command | grep '[L]oginWindow.app/LoginWindow' | awk '{print $1}'; }
# GNUstep starts slowly under QEMU/TCG (minutes): wait for the login
# window (title "Login") to be mapped, then let it finish drawing.
wait_greeter() {
    i=0
    while [ $i -lt $W ]; do
        $X/x11_probe tree 2>/dev/null | grep 'name=Login$' | grep -q ' viewable' && break
        sleep 5; i=$((i + 5))
    done
    echo "login window after ${i}s (greeter $(greeter_pids))"
    sleep ${SETTLE:-30}
}

echo "== login window"
wait_greeter
shot 1_users

echo "== wrong password"
# Panel 620x460 centered on 1024x768; alice's tile is the first of two.
$X/x11_probe click 450 414
sleep 30
shot 2_password
$X/xtest_type nottherightone
sleep 45
shot 3_wrong
grep -c 'failed login for "alice"' $T/gsdm.log | sed 's/^/failed logins: /'
echo "greeter still up: $(greeter_pids)"

echo "== right password"
$X/xtest_type wonderland
sleep ${SESSION_WAIT:-180}
shot 4_session
ps -ax -o user,pid,command | grep -E '[w]maker|[T]erminal|[X]session'
ls -l /Users/alice/.Xauthority
echo "greeter after login: $(greeter_pids)"

echo "== logout"
WM=$(ps -ax -o pid,command | grep '[w]maker' | awk '{print $1}')
kill $WM
sleep 20
wait_greeter
shot 5_again

echo "== stop gsdm"
kill $GP
sleep 15
ps -ax -o pid,command | grep -E '[X]vfb|[L]oginWindow|[g]sdm -config' || echo "nothing left"
wait $GP; echo "gsdm-rc=$?"
echo "== gsdm.log"
grep -v 'offsets for style\|^>\|xkbcomp\|XKEYBOARD' $T/gsdm.log
echo "GSDM_TEST DONE"
