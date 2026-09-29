#!/bin/sh
# DAR-340/C1 regression: dyld must NOT honor DYLD_INSERT_LIBRARIES in a
# setuid or __RESTRICT image, and MUST still honor it for a plain binary.
# Two phases (this project's su needs a tty for its password prompt, so the
# test phase is run from a real non-root login instead of via su):
#   as ROOT:      dyld-inject-regression prep     (after /usr/sbin/provision-setuid)
#   as NON-ROOT:  dyld-inject-regression test     (e.g. logged in as alice)
# Needs /usr/lib/dyld_inject_probe.dylib (build_dyld_inject_probe.sh).
# `test` exits 0 only if every check PASSes. See docs/sudo-su-dyld-regression.md.
PROBE=/usr/lib/dyld_inject_probe.dylib
OUTF=/tmp/dyld_inject_probe.out
# launchd empties /tmp and /var/tmp at every boot (pd_launchd_boot), so the
# prepared binaries live in their own directory instead.
D=/var/dyld_regression
case "$1" in
prep)
    [ "$(id -u)" = 0 ] || { echo "prep must run as root" >&2; exit 2; }
    [ -f "$PROBE" ] || { echo "FAIL: $PROBE missing" >&2; exit 2; }
    chmod 1777 /tmp; mkdir -p $D && chmod 755 $D
    # Setuid copy of a binary WITHOUT a __RESTRICT section: tests the
    # issetugid() path of the dyld policy shim alone. Plain copy = control.
    cp /bin/id $D/id_suid && chown root:wheel $D/id_suid && chmod 4755 $D/id_suid
    cp /bin/id $D/id_plain && chmod 755 $D/id_plain
    # Non-setuid copy of su: tests the __RESTRICT path alone.
    cp /usr/bin/su $D/su_nosuid && chmod 755 $D/su_nosuid
    ls -ln $D/id_suid $D/id_plain $D/su_nosuid /usr/bin/su /usr/bin/sudo
    rm -f "$OUTF"
    exit 0 ;;
test) ;;
*) echo "usage: $0 prep|test" >&2; exit 2 ;;
esac
[ "$(id -u)" != 0 ] || { echo "test must run as a NON-root user" >&2; exit 2; }
fail=0
n() { [ -f "$OUTF" ] && grep -c "" "$OUTF" || echo 0; }
# A failed setuid check leaves a root-owned $OUTF a non-root rm cannot remove
# (sticky /tmp), so count only lines added since this run() started.
base=0
run() { rm -f "$OUTF" 2>/dev/null; base=$(n); env DYLD_INSERT_LIBRARIES=$PROBE "$@" </dev/null >/dev/null 2>&1; }
chk_no() { # <label>
    if [ "$(n)" -le "$base" ]; then echo "PASS $1: constructor did NOT run"; else echo "FAIL $1: constructor ran: $(cat $OUTF)"; fail=1; fi
}
run $D/id_plain
if [ "$(n)" -gt "$base" ]; then echo "PASS control: constructor ran in a plain binary"; else echo "FAIL control: constructor did NOT run (probe broken; other results meaningless)"; fail=1; fi
run $D/id_suid;              chk_no "setuid id (issetugid path)"
for b in su sudo; do
    if [ ! -u /usr/bin/$b ]; then echo "SKIP $b: not setuid (run /usr/sbin/provision-setuid)"; continue; fi
    if [ $b = sudo ]; then run /usr/bin/sudo -V; else run /usr/bin/su -; fi
    chk_no "real /usr/bin/$b"
done
run $D/su_nosuid -;          chk_no "__RESTRICT su copy, non-setuid"
rm -f $D/id_suid $D/id_plain $D/su_nosuid "$OUTF" 2>/dev/null
[ $fail -eq 0 ] && echo "dyld-inject-regression: ALL PASS" || echo "dyld-inject-regression: FAILED"
exit $fail
