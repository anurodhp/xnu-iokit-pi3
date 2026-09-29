#!/bin/sh
# DAR-340/C1 regression: dyld must NOT honor DYLD_INSERT_LIBRARIES in a
# setuid or __RESTRICT image, and MUST still honor it for a plain binary.
# Run as ROOT inside the booted target, AFTER /usr/sbin/provision-setuid
# (needs /usr/lib/dyld_inject_probe.dylib from build_dyld_inject_probe.sh).
# Exit 0 = all checks PASS. See docs/sudo-su-dyld-regression.md.
PROBE=/usr/lib/dyld_inject_probe.dylib
OUTF=/tmp/dyld_inject_probe.out
U=dyldtest
fail=0
[ "$(id -u)" = 0 ] || { echo "must run as root" >&2; exit 2; }
[ -f "$PROBE" ] || { echo "FAIL: $PROBE missing" >&2; exit 2; }
if ! id "$U" >/dev/null 2>&1; then
    pw useradd -n "$U" -u 5999 -m -s /bin/sh -w no || { echo "FAIL: pw useradd $U" >&2; exit 2; }
fi
chmod 1777 /tmp
# Setuid copy of a binary WITHOUT a __RESTRICT section: tests the issetugid()
# path of the dyld policy shim alone. Plain copy: tests the control.
cp /usr/bin/id /tmp/id_suid && chown root:wheel /tmp/id_suid && chmod 4755 /tmp/id_suid
cp /usr/bin/id /tmp/id_plain && chmod 755 /tmp/id_plain
ls -ln /tmp/id_suid /tmp/id_plain /usr/bin/su /usr/bin/sudo 2>&1

run() { # <label> <command run as $U>
    rm -f "$OUTF"
    su "$U" -c "$2" </dev/null >/dev/null 2>&1
}
hit() { [ -f "$OUTF" ] && grep -c "" "$OUTF" || echo 0; }
rootline() { [ -f "$OUTF" ] && grep -c "euid=0" "$OUTF" || echo 0; }

# 1. control: plain binary must load the probe (proves probe + su -c work).
run control "DYLD_INSERT_LIBRARIES=$PROBE /tmp/id_plain"
if [ "$(hit)" -ge 1 ]; then echo "PASS control: constructor ran in a plain binary"; else echo "FAIL control: constructor did NOT run (probe/harness broken, other results are meaningless)"; fail=1; fi

# 2. setuid, no __RESTRICT: issetugid() path.
run suid "DYLD_INSERT_LIBRARIES=$PROBE /tmp/id_suid"
if [ "$(hit)" -eq 0 ]; then echo "PASS setuid id: constructor did NOT run"; else echo "FAIL setuid id: constructor ran: $(cat $OUTF)"; fail=1; fi

# 3/4. the real binaries.
for b in su sudo; do
    arg=""; [ "$b" = sudo ] && arg="-V"
    if [ ! -u /usr/bin/$b ]; then echo "SKIP $b: not setuid (run /usr/sbin/provision-setuid) -- result would be vacuous"; continue; fi
    run $b "DYLD_INSERT_LIBRARIES=$PROBE /usr/bin/$b $arg"
    if [ "$(rootline)" -eq 0 ] && [ "$(hit)" -eq 0 ]; then echo "PASS $b: constructor did NOT run"; else echo "FAIL $b: constructor ran: $(cat $OUTF)"; fail=1; fi
done

# 5. __RESTRICT alone (non-setuid copy of su).
cp /usr/bin/su /tmp/su_nosuid && chmod 755 /tmp/su_nosuid
run restrict "DYLD_INSERT_LIBRARIES=$PROBE /tmp/su_nosuid $U -c true"
if [ "$(hit)" -eq 0 ]; then echo "PASS __RESTRICT su copy (non-setuid): constructor did NOT run"; else echo "FAIL __RESTRICT su copy: constructor ran: $(cat $OUTF)"; fail=1; fi

rm -f /tmp/id_suid /tmp/id_plain /tmp/su_nosuid "$OUTF"
[ $fail -eq 0 ] && echo "dyld-inject-regression: ALL PASS" || echo "dyld-inject-regression: FAILED"
exit $fail
