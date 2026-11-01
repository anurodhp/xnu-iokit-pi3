#!/bin/bash
# Screenshots of the darwin-util apps (Console, Activity, Preview) on the real
# desktop, taken in QEMU: copies tools/darwin_util_screenshots_guest.sh and a
# sample image onto the image, boots it, runs the script as root (Xvfb, gsdm's
# Xsession with Window Maker, Naples and Milan, then each app; see that script),
# and writes each screenshot to <darwin-util>/<App>/doc/screenshot.png, the
# picture at the top of that app's README.
# Needs an image injected after the apps were built (build_console.sh,
# build_activity.sh, build_preview.sh) with the X server, xwd, x11_probe and
# the GNUstep desktop on it, e.g. a fresh tools/make_sd_image.sh image.
# Usage: tools/darwin_util_screenshots_qemu.sh [image]
#   DARWIN_UTIL_DIR  the darwin-util checkout (default ../darwin-util)
set -e
cd "$(dirname "$0")/.."
IMG="${1:-qemu/darwin_util_shots.img}"
DU="${DARWIN_UTIL_DIR:-../darwin-util}"
[ -f "$IMG" ] || { echo "error: $IMG not found (tools/make_sd_image.sh $IMG)" >&2; exit 1; }
[ -d "$DU/Preview" ] || { echo "error: $DU is not the darwin-util checkout; set DARWIN_UTIL_DIR" >&2; exit 1; }

# with_root <command...>: attach $IMG, mount its HFS+ root (slice 2) as $MNT,
# run the command, detach (as gsdm_qemu_test.sh).
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
    for a in Console Activity Preview; do
        [ -x "$MNT/Applications/$a.app/$a" ] || { echo "error: $a.app is not on $IMG -- build it and re-inject" >&2; return 1; }
    done
    rm -rf "$MNT/var/root/shots"
    mkdir -p "$MNT/var/root/shots"
    cp tools/darwin_util_screenshots_guest.sh "$MNT/var/root/darwin_util_shots.sh"
    python3 "$DU/Preview/tools/make_sample_image.py" "$MNT/var/root/shots/Lake.png"
}
get_results() {
    local a
    mkdir -p qemu/darwin_util_shots
    cp "$MNT"/var/root/shots/*.log qemu/darwin_util_shots/ 2>/dev/null || true
    for a in Console Activity Preview; do
        if [ -f "$MNT/var/root/shots/$a.xwd" ]; then
            mkdir -p "$DU/$a/doc"
            python3 tools/xwd_to_png.py "$MNT/var/root/shots/$a.xwd" "$DU/$a/doc/screenshot.png" | head -1
            echo "wrote $DU/$a/doc/screenshot.png"
        else
            echo "missing: no $a screenshot (see qemu/darwin_util_shots/ and qemu/darwin_util_shots_boot.log)"
        fi
    done
}

with_root put_script
rc=0
SD_IMAGE="$IMG" tools/boot_test.py --timeout 3600 --login --log qemu/darwin_util_shots_boot.log \
    --expect 'SHOTS DONE' --expect-fail 'panic' \
    --cmd 'sh /var/root/darwin_util_shots.sh 2>&1' || rc=$?
with_root get_results
exit $rc
