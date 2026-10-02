#!/bin/bash
# DAR-465 real-hardware repro: push and pull N MB through ssh to the Pi,
# repeatedly, and report the first iteration where the Ethernet wedges.
#
# Host side only. It never touches the serial port; capture that in a second
# terminal at the same time (the port must be free - quit minicom first):
#
#   tools/realhw_serial_test.py --raw --timeout 1800 --log /tmp/dar465-serial.log
#
# The wedge this hunts (DAR-465, real Pi 3, image pi3-20261001-2251): after a
# few MB of TCP traffic the LAN9514 bulk OUT pipe STALLs on every frame,
# serial floods with "SMSC9514FamilyTransport: bulk OUT stalled ..." and
# "bulk OUT of 82/106 bytes failed (0xe000404f)", ssh/ping time out, only a
# reboot recovers. The trigger was a 1.5 MB push followed by a ~3.7 MB pull;
# the pull (Pi transmitting full-size frames back to back) is the suspect.
#
# Environment:
#   PI_HOST     default 10.0.0.142
#   PI_USER     default root
#   PI_PASS     optional. If set, fed to ssh through a throw-away SSH_ASKPASS
#               helper (OpenSSH >= 8.4, SSH_ASKPASS_REQUIRE=force), so no
#               sshpass is needed. Unset = key auth (BatchMode).
#   MB          payload size per direction, default 4
#   ITER        iterations, default 20
#   XFER_TIMEOUT seconds one push or pull may take before it counts as a
#               wedge, default 120
#   SSH_OPTS    extra ssh options
#
# Exit status: 0 all iterations passed, 1 wedged (or data mismatch), 2 setup.
set -u

PI_HOST="${PI_HOST:-10.0.0.142}"
PI_USER="${PI_USER:-root}"
MB="${MB:-4}"
ITER="${ITER:-20}"
XFER_TIMEOUT="${XFER_TIMEOUT:-120}"
REMOTE=/tmp/eth_stress.bin

WORK="$(mktemp -d -t eth_stress)"
trap 'rm -rf "$WORK"' EXIT

SSH=(ssh -o ConnectTimeout=10 -o ServerAliveInterval=5 -o ServerAliveCountMax=3
     -o StrictHostKeyChecking=accept-new ${SSH_OPTS:-})
if [ -n "${PI_PASS:-}" ]; then
	printf '#!/bin/sh\nprintf "%%s\\n" "$ETH_STRESS_PASS"\n' > "$WORK/askpass"
	chmod 700 "$WORK/askpass"
	export ETH_STRESS_PASS="$PI_PASS" SSH_ASKPASS="$WORK/askpass" SSH_ASKPASS_REQUIRE=force
	export DISPLAY="${DISPLAY:-:0}"
	SSH+=(-o PreferredAuthentications=password,keyboard-interactive -o NumberOfPasswordPrompts=1)
else
	SSH+=(-o BatchMode=yes)
fi
SSH+=("$PI_USER@$PI_HOST")

ts() { date '+%H:%M:%S'; }

# Run "$@" with a wall-clock limit (macOS has no timeout(1) by default).
run_limited() {
	local limit=$1; shift
	"$@" &
	local pid=$!
	( sleep "$limit"; kill -TERM "$pid" 2>/dev/null; sleep 2; kill -KILL "$pid" 2>/dev/null ) &
	local dog=$!
	wait "$pid"; local rc=$?
	kill "$dog" 2>/dev/null; wait "$dog" 2>/dev/null
	return $rc
}

wedged() {
	echo "$(ts) WEDGE at iteration $1: $2"
	if ping -c 3 -t 5 "$PI_HOST" >/dev/null 2>&1; then
		echo "$(ts) ping $PI_HOST: still answers"
	else
		echo "$(ts) ping $PI_HOST: NO answer (link wedged)"
	fi
	echo "Stop the serial capture now and keep its log; see docs/dar-465-ethernet-stall.md"
	exit 1
}

echo "$(ts) generating ${MB} MB payload"
dd if=/dev/urandom of="$WORK/payload" bs=1048576 count="$MB" 2>/dev/null || exit 2
WANT="$(shasum -a 256 "$WORK/payload" | cut -d' ' -f1)"

if ! run_limited 30 "${SSH[@]}" true; then
	echo "cannot ssh to $PI_USER@$PI_HOST (set PI_PASS, or install a key)"; exit 2
fi

for i in $(seq 1 "$ITER"); do
	t0=$(date +%s)
	if ! run_limited "$XFER_TIMEOUT" "${SSH[@]}" "cat > $REMOTE" < "$WORK/payload"; then
		wedged "$i" "push of ${MB} MB failed or exceeded ${XFER_TIMEOUT}s"
	fi
	t1=$(date +%s)
	if ! run_limited "$XFER_TIMEOUT" "${SSH[@]}" "cat $REMOTE" > "$WORK/back"; then
		wedged "$i" "pull of ${MB} MB failed or exceeded ${XFER_TIMEOUT}s ($(wc -c < "$WORK/back") bytes arrived)"
	fi
	t2=$(date +%s)
	GOT="$(shasum -a 256 "$WORK/back" | cut -d' ' -f1)"
	if [ "$GOT" != "$WANT" ]; then
		wedged "$i" "pulled data differs from what was pushed ($(wc -c < "$WORK/back") bytes)"
	fi
	echo "$(ts) iter $i/$ITER ok: push $((t1 - t0))s, pull $((t2 - t1))s"
done

"${SSH[@]}" "rm -f $REMOTE" >/dev/null 2>&1
echo "$(ts) PASS: $ITER x ${MB} MB each way"
exit 0
