#!/bin/sh
# btsetup -- the Bluetooth settings the desktop shows (Naples' menu bar
# panel, Settings' Bluetooth pane), over NetBSD's tools on netbt
# (docs/bluetooth-stack-plan.md). It does what "Using it on the Pi" does by
# hand and prints tab-separated lines a program can read. Installed as
# /usr/sbin/btsetup; needs root, as the tools do.
#
#   btsetup status
#       power<TAB>on|off|none        none: no unit bcmbt0 (no bt=1)
#       device<TAB>addr<TAB>state<TAB>name     per paired input device;
#                                    state: connected, connecting, disconnected
#   btsetup power on|off
#       Turns the unit on (up, page scan, Secure Simple Pairing) or off, and
#       remembers off in /etc/bluetooth.off for the boot job.
#   btsetup scan
#       found<TAB>addr<TAB>kind<TAB>name       per device in range (10 s);
#                                    kind: keyboard, mouse, combo, phone,
#                                    computer, audio, other
#   btsetup pair addr kind [name]
#       Authorizes pairing (btpin), reads the device's HID record over SDP
#       and attaches it (btdevctl -A; bthidd connects, which pairs), then
#       waits up to 90 s for it to connect. Prints, as they happen:
#       pin<TAB>NNNNNN     a keyboard without SSP asked for a PIN: type it on
#                          the keyboard
#       passkey<TAB>NNNNNN a keyboard with SSP: type it on the keyboard
#       paired             connected
#       error<TAB>message  and exits 1
#   btsetup forget addr
#       Detaches the device and removes its link key and name.
#
# Names are kept in /var/db/bluetooth-names (addr<TAB>name), not in
# /etc/bluetooth/hosts, whose names end at the first space.
PATH=/usr/sbin:/usr/bin:/sbin:/bin
DEV=bcmbt0
NAMES=/var/db/bluetooth-names
STATUS=/var/run/bthidd.status
OFF=/etc/bluetooth.off
HCIDLOG=/var/log/bthcid.log
TAB=$(printf '\t')

die() { printf 'error\t%s\n' "$*"; exit 1; }

valid_addr() {
	echo "$1" | grep -Eq '^[0-9a-fA-F]{2}(:[0-9a-fA-F]{2}){5}$'
}

name_of() {
	[ -f "$NAMES" ] && awk -F "$TAB" -v a="$1" 'tolower($1) == tolower(a) { n = $2 } END { if (n != "") print n }' "$NAMES"
}

set_name() { # addr name (empty: remove it)
	tmp="$NAMES.new.$$"
	{
		if [ -f "$NAMES" ]; then awk -F "$TAB" -v a="$1" 'tolower($1) != tolower(a)' "$NAMES"; fi
		if [ -n "$2" ]; then printf '%s\t%s\n' "$1" "$2"; fi
	} > "$tmp" && mv "$tmp" "$NAMES"
}

unit_up() {
	btconfig "$DEV" 2>/dev/null | head -1 | grep -q '<UP'
}

# Bytes in a file (the image has no tr to trim wc's padding)
file_size() {
	if [ -f "$1" ]; then wc -c < "$1" | awk '{ print $1 }'; else echo 0; fi
}

cmd_status() {
	if ! btconfig "$DEV" >/dev/null 2>&1; then
		printf 'power\tnone\n'
		return 0
	fi
	if unit_up; then printf 'power\ton\n'; else printf 'power\toff\n'; fi
	[ -f "$STATUS" ] || return 0
	while read -r a st; do
		[ -n "$a" ] || continue
		printf 'device\t%s\t%s\t%s\n' "$a" "$st" "$(name_of "$a")"
	done < "$STATUS"
}

cmd_power() {
	case "$1" in
	on)
		btconfig "$DEV" up pscan ssp >/dev/null || die "Bluetooth could not be turned on."
		rm -f "$OFF"
		# bthidd connects the devices that wait for the host
		[ -f /var/run/bthidd.pid ] && kill -HUP "$(cat /var/run/bthidd.pid)" 2>/dev/null
		;;
	off)
		btconfig "$DEV" down >/dev/null || die "Bluetooth could not be turned off."
		: > "$OFF"
		;;
	*)	die "usage: btsetup power on|off" ;;
	esac
}

# A device's kind from its Class of Device (Bluetooth Assigned Numbers,
# "Class of Device": major class in bits 8-12, peripheral minor in 6-7)
kind_of() {
	c=$(printf '%d' "$1" 2>/dev/null) || { echo other; return; }
	major=$(( (c >> 8) & 31 ))
	case $major in
	1) echo computer ;;
	2) echo phone ;;
	4) echo audio ;;
	5)	case $(( (c >> 6) & 3 )) in
		1) echo keyboard ;;
		2) echo mouse ;;
		3) echo combo ;;
		*) echo other ;;
		esac ;;
	*) echo other ;;
	esac
}

cmd_scan() {
	unit_up || die "Bluetooth is off."
	btconfig "$DEV" inquiry 2>/dev/null | awk '
		/^ *[0-9]+: bdaddr / { if (a != "") print a "\t" c "\t" n; a = $3; c = ""; n = "" }
		/^   : name "/ { n = $0; sub(/^   : name "/, "", n); sub(/"$/, "", n) }
		/^   : class \[/ { c = $0; sub(/^   : class \[/, "", c); sub(/\].*/, "", c) }
		END { if (a != "") print a "\t" c "\t" n }' |
	while IFS="$TAB" read -r a c n; do
		printf 'found\t%s\t%s\t%s\n' "$a" "$(kind_of "${c:-0}")" "$n"
	done
}

cmd_pair() { # addr kind name
	a=$1 kind=$2 name=$3
	valid_addr "$a" || die "usage: btsetup pair addr kind [name]"
	unit_up || die "Bluetooth is off."
	[ -n "$name" ] && set_name "$a" "$name"
	# bthcid answers only devices btpin authorized. A keyboard without SSP
	# needs a PIN typed on it; other devices without SSP mostly use 0000.
	# With SSP the PIN is not used: a keyboard gets a passkey (below), a
	# mouse pairs by Just Works. So the PIN is shown only once bthcid logs
	# the device's PIN_Code_Request.
	logsize=$(file_size "$HCIDLOG")
	case "$kind" in
	keyboard|combo)
		pin=$(btpin -d "$DEV" -a "$a" -r -l 6 2>/dev/null | sed -n 's/^PIN: *//p')
		[ -n "$pin" ] || die "Pairing could not be started." ;;
	*)
		pin=""
		btpin -d "$DEV" -a "$a" -p 0000 >/dev/null 2>&1 || die "Pairing could not be started." ;;
	esac
	if ! out=$(btdevctl -A -d "$DEV" -a "$a" -s HID 2>&1); then
		case "$out" in
		*"not found"*)	# sdp.c: "service HID not found"
			die "Only keyboards, mice and other input devices can be used for now." ;;
		*)	die "The device did not answer. Make sure it is in pairing mode." ;;
		esac
	fi
	shown=""
	pinshown=""
	i=0
	while [ $i -lt 90 ]; do
		if [ -f "$HCIDLOG" ]; then
			new=$(tail -c +$((logsize + 1)) "$HCIDLOG" 2>/dev/null)
			pk=$(printf '%s\n' "$new" | grep -i "passkey for $a: " | tail -1 | sed 's/.*: \([0-9][0-9]*\).*/\1/')
			if [ -n "$pk" ] && [ "$pk" != "$shown" ]; then
				printf 'passkey\t%s\n' "$pk"
				shown=$pk
			fi
			if [ -n "$pin" ] && [ -z "$pinshown" ] &&
			    printf '%s\n' "$new" | grep -qi "PIN_Code_Request event from .*$a"; then
				printf 'pin\t%s\n' "$pin"
				pinshown=1
			fi
		fi
		if [ -f "$STATUS" ] && grep -qi "^$a connected" "$STATUS"; then
			printf 'paired\n'
			return 0
		fi
		sleep 1
		i=$((i + 1))
	done
	btdevctl -D -d "$DEV" -a "$a" -s HID >/dev/null 2>&1
	die "Pairing did not finish."
}

cmd_forget() {
	a=$1
	valid_addr "$a" || die "usage: btsetup forget addr"
	btconfig "$DEV" >/dev/null 2>&1 || die "Bluetooth is not available."
	# The tools find the unit only while it is up (libbluetooth bt_devaddr):
	# with Bluetooth off, bring it up for this without page scan, so nothing
	# connects, and take it down again.
	wasdown=""
	if ! unit_up; then
		btconfig "$DEV" up >/dev/null 2>&1 || die "Bluetooth could not be started to forget the device."
		wasdown=1
	fi
	btdevctl -D -d "$DEV" -a "$a" -s HID >/dev/null 2>&1
	btkey -d "$DEV" -a "$a" -c >/dev/null 2>&1
	btkey -d "$DEV" -a "$a" -C >/dev/null 2>&1
	[ -n "$wasdown" ] && btconfig "$DEV" down >/dev/null 2>&1
	set_name "$a" ""
	return 0
}

case "$1" in
status) cmd_status ;;
power)  cmd_power "$2" ;;
scan)   cmd_scan ;;
pair)   cmd_pair "$2" "$3" "$4" ;;
forget) cmd_forget "$2" ;;
*)      echo "usage: btsetup status | power on|off | scan | pair addr kind [name] | forget addr" >&2; exit 2 ;;
esac
