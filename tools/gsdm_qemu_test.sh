#!/bin/bash
# QEMU test of gsdm (the graphical login): copies tools/gsdm_verify_guest.sh
# onto the image, boots it, runs it as root (gsdm with its real greeter and
# Xsession on Xvfb; see that script for the steps), then copies the guest's
# screenshots and gsdm log back out as qemu/gsdm_*.png / qemu/gsdm.log.
# Needs an image injected after build_gsdm.sh (and the X server, GNUstep,
# wmaker, xwd, x11_probe and xtest_type, which a QEMU image gets from
# inject_into_sd_image.sh).
# Usage: tools/gsdm_qemu_test.sh [image]
set -e
cd "$(dirname "$0")/.."
IMG="${1:-qemu/gsdm_test.img}"
[ -f "$IMG" ] || { echo "error: $IMG not found" >&2; exit 1; }

# with_root <command...>: attach $IMG, mount its HFS+ root (slice 2) as $MNT,
# run the command, detach.
with_root() {
    local disk slice
    disk=$(hdiutil attach -nomount "$IMG" | awk 'NR==1{print $1}')
    [ -n "$disk" ] || { echo "error: hdiutil attach failed" >&2; exit 1; }
    slice="${disk}s2"
    if ! diskutil mount "$slice" >/dev/null; then
        fsck_hfs -fy "/dev/r${slice#/dev/}" >/dev/null
        diskutil mount "$slice" >/dev/null
    fi
    MNT=$(diskutil info "$slice" | awk -F': +' '/Mount Point/{print $2}')
    "$@" || { hdiutil detach "$disk" >/dev/null; return 1; }
    diskutil unmount "$slice" >/dev/null 2>&1 || true
    hdiutil detach "$disk" >/dev/null
}
put_script() {
    mkdir -p "$MNT/var/root"
    cp tools/gsdm_verify_guest.sh "$MNT/var/root/gsdm_verify.sh"
    rm -rf "$MNT/var/root/gsdmtest"
}
get_results() {
    local f n
    for f in "$MNT"/var/root/gsdmtest/*.xwd; do
        [ -f "$f" ] || continue
        n=$(basename "$f" .xwd)
        python3 tools/xwd_to_png.py "$f" "qemu/gsdm_$n.png" | head -1
    done
    cp "$MNT/var/root/gsdmtest/gsdm.log" qemu/gsdm.log 2>/dev/null || true
    cp "$MNT/Users/alice/.xsession-errors" qemu/gsdm_alice_xsession-errors.log 2>/dev/null || true
}

with_root put_script
rc=0
SD_IMAGE="$IMG" tools/boot_test.py --timeout 1800 --login --log qemu/gsdm_boot.log \
    --expect 'GSDM_TEST DONE' --expect-fail 'panic' \
    --cmd 'sh /var/root/gsdm_verify.sh 2>&1' || rc=$?
with_root get_results
ls qemu/gsdm_*.png 2>/dev/null
exit $rc
