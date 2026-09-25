#!/bin/sh
# fake-hwclock (DAR-330) -- keep a Pi's clock from starting at 1970.
#
# The Raspberry Pi 3 has no battery-backed real-time clock: every boot
# starts at the epoch until chronyd reaches an NTP server, and a 56-year
# step while daemons are already running is exactly what DAR-377's sshd
# sessions tripped over. Same idea and same interface as Raspberry Pi OS's
# fake-hwclock: remember the time on disk, restore it at boot.
#
#   fake-hwclock load   set the clock from $FILE, only if that is later
#                       than the current clock (never moves time back)
#   fake-hwclock save   write the current time to $FILE
#   fake-hwclock        (as the org.puredarwin.fake-hwclock LaunchDaemon)
#                       load, then save every $INTERVAL seconds, and once
#                       more on SIGTERM -- which launchd sends every job on
#                       an orderly shutdown (DAR-334)
#
# The saved value is seconds since the epoch (`date +%s`), restored with
# `date -f %s`, both real options of this image's Apple date(1).
FILE=/var/db/fake-hwclock
INTERVAL=900

load() {
	[ -f "$FILE" ] || return 0
	saved=$(cat "$FILE")
	now=$(date +%s)
	case "$saved" in
	''|*[!0-9]*) echo "fake-hwclock: ignoring malformed $FILE" >&2; return 0 ;;
	esac
	if [ "$saved" -gt "$now" ]; then
		date -f %s "$saved" >/dev/null && echo "fake-hwclock: clock set from $FILE"
	fi
}

save() {
	mkdir -p "$(dirname "$FILE")"
	date +%s > "$FILE.new" && mv "$FILE.new" "$FILE"
}

case "$1" in
load) load ;;
save) save ;;
'')
	load
	# sleep in the background so the trap runs as soon as TERM arrives
	# (bash defers traps until a foreground child exits), and reap it so
	# no orphaned sleep outlives the job.
	sleeper=
	trap '[ -n "$sleeper" ] && kill "$sleeper" 2>/dev/null; save; exit 0' TERM INT
	while :; do
		sleep "$INTERVAL" &
		sleeper=$!
		wait "$sleeper"
		save
	done
	;;
*) echo "usage: fake-hwclock [load|save]" >&2; exit 64 ;;
esac
