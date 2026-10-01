#!/bin/bash
# DAR-439: install the qostest launchd job into an image built by
# make_sd_image.sh. Usage: tools/wm_qos_prep_image.sh qemu/x.img base|x|w|xw [wm] [ptype]
#   base|x|w|xw  which /var/root/qos_mode the job runs
#   wm        optional extra wmaker binary to install as /usr/X11/bin/wmaker.base
#   ptype     ProcessType for the job (default Interactive; App also of interest)
set -e
cd "$(dirname "$0")/.."
IMG=$1; MODE=$2; WMB=$3; PTYPE=${4:-Interactive}
DEV=$(hdiutil attach -nomount -imagekey diskimage-class=CRawDiskImage "$IMG" | awk 'NR==1{print $1}')
[ -n "$DEV" ] || { echo "attach failed" >&2; exit 1; }
echo "attached $DEV"
trap 'diskutil unmountDisk "$DEV" >/dev/null 2>&1; hdiutil detach "$DEV" >/dev/null 2>&1 || true' EXIT
ROOT=${DEV}s2
diskutil mount "$ROOT" >/dev/null
MNT=$(diskutil info "$ROOT" | awk -F': +' '/Mount Point/{print $2}')
echo "root mounted at $MNT"
mkdir -p "$MNT/var/root" "$MNT/Library/LaunchDaemons"
mkdir -p "$MNT/usr/lib"; cp tools/userland_staging/libc_build/qos_boost.dylib "$MNT/usr/lib/"
cp tools/wm_qos_verify_guest.sh "$MNT/var/root/qos_job.sh"
echo "$MODE" > "$MNT/var/root/qos_mode"
sed "s|<string>Interactive</string>|<string>$PTYPE</string>|" tools/userland_staging/org.puredarwin.qostest.plist > "$MNT/Library/LaunchDaemons/org.puredarwin.qostest.plist"
chmod 644 "$MNT/Library/LaunchDaemons/org.puredarwin.qostest.plist"
[ -n "$WMB" ] && { cp "$WMB" "$MNT/usr/X11/bin/wmaker.base"; chmod 755 "$MNT/usr/X11/bin/wmaker.base"; }
sync
echo done
