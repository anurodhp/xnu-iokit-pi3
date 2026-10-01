#!/bin/bash
# DAR-434: add the test script and probes to an image built by make_sd_image.sh.
# Usage: tools/wm_spawn_prep_image.sh qemu/x.img
set -e
cd "$(dirname "$0")/.."
IMG=$1
DEV=$(hdiutil attach -nomount -imagekey diskimage-class=CRawDiskImage "$IMG" | awk 'NR==1{print $1}')
[ -n "$DEV" ] || { echo "attach failed" >&2; exit 1; }
echo "attached $DEV"
trap 'diskutil unmountDisk "$DEV" >/dev/null 2>&1; hdiutil detach "$DEV" >/dev/null 2>&1 || true' EXIT
ROOT=${DEV}s2
diskutil mount "$ROOT" >/dev/null
MNT=$(diskutil info "$ROOT" | awk -F': +' '/Mount Point/{print $2}')
echo "root mounted at $MNT"
mkdir -p "$MNT/var/root"
for p in wmclose_probe qos_probe x11_probe wmlat spawn_test; do
    cp tools/userland_staging/$p.macho "$MNT/usr/X11/bin/$p"; chmod 755 "$MNT/usr/X11/bin/$p"
done
cp tools/wm_spawn_verify_guest.sh "$MNT/var/root/wmsp.sh"
sync
echo done
