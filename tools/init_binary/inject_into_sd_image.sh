#!/bin/bash
# Rebuilds init.macho and pwtool.macho and writes them into an SD card
# image's real HFS+ root (/sbin/launchd, /sbin/pwtool), replacing whatever's
# there. This is a manual macOS-only hdiutil/diskutil operation (attach the
# raw MBR image, mount just the HFS+ partition, copy, unmount, detach) that
# had previously been done by hand each time init.c changed -- see project
# memory (userspace libc/dyld) for why this needed a script.
#
# Also ensures /etc exists on the image -- fresh images have no /etc dir,
# so pwtool's lock-file open(O_CREAT, "/etc/.pwtool.lock") fails ENOENT on
# first use. Same class of gap as /dev needing to exist for devfs to mount
# onto (see project memory), fixed here the same way: create it host-side
# rather than teaching pwtool to mkdir its own lock directory.
#
# If the image has no /etc/master.passwd yet, also provisions a real
# root account (password "darwin") directly onto the mounted volume via
# tools/pwtool/hash_password_host.c (a real host-built hasher, reusing
# crypt_des.c). This closes the chicken-and-egg gap left by removing
# init.c's implicit-root-on-no-accounts fallback (see init.c's
# LOGIN_NO_ACCOUNTS handling): there is no other way to get a FIRST
# account onto a fresh image, since pwtool.c itself only runs on the
# booted target and needs a shell -- which now needs a login -- which
# needs an account. Only ever runs on a genuinely account-less image;
# never overwrites an existing /etc/master.passwd.
#
# Usage: tools/init_binary/inject_into_sd_image.sh [SD_IMAGE] [HFS_PARTITION_INDEX]
#   SD_IMAGE:             defaults to qemu/hfs_root_test.img (runvm.sh's default)
#   HFS_PARTITION_INDEX:  1-based fdisk slice number for the HFS+ root
#                         partition, defaults to 2 (matches runvm.sh's
#                         default ROOT_DEV=disk0s2)
set -e
cd "$(dirname "$0")/../.."

SD_IMAGE="${1:-qemu/hfs_root_test.img}"
PART_IDX="${2:-2}"

if [ ! -f "$SD_IMAGE" ]; then
    echo "error: SD card image not found: $SD_IMAGE" >&2
    exit 1
fi

python3 tools/init_binary/build.py
python3 tools/pwtool/build.py

DISK=$(hdiutil attach -nomount "$SD_IMAGE" | awk 'NR==1{print $1}')
if [ -z "$DISK" ]; then
    echo "error: hdiutil attach did not report a device node" >&2
    exit 1
fi
SLICE="${DISK}s${PART_IDX}"
echo "+ attached $SD_IMAGE as $DISK (root partition: $SLICE)"

cleanup() {
    diskutil unmount "$SLICE" >/dev/null 2>&1 || true
    hdiutil detach "$DISK" >/dev/null 2>&1 || true
}
trap cleanup EXIT

diskutil mount "$SLICE"
MNT=$(diskutil info "$SLICE" | awk -F': +' '/Mount Point/{print $2}')
if [ -z "$MNT" ]; then
    echo "error: could not determine mount point for $SLICE" >&2
    exit 1
fi

cp tools/init_binary/init.macho "$MNT/sbin/launchd"
chmod 700 "$MNT/sbin/launchd"
echo "+ wrote $(wc -c < tools/init_binary/init.macho) bytes to $MNT/sbin/launchd"

cp tools/pwtool/pwtool.macho "$MNT/sbin/pwtool"
chmod 700 "$MNT/sbin/pwtool"
echo "+ wrote $(wc -c < tools/pwtool/pwtool.macho) bytes to $MNT/sbin/pwtool"

mkdir -p "$MNT/etc"
echo "+ ensured $MNT/etc exists (pwtool lock-file/master.passwd/group directory)"

mkdir -p "$MNT/tmp"
echo "+ ensured $MNT/tmp exists (real fopen()-family O_CREAT target dir -- same"
echo "  class of gap as /etc and /dev above; found via a real QEMU boot-test"
echo "  FAIL when tools/userland_staging/libc_stdio_smoketest.macho's real"
echo "  fopen(\"/tmp/...\", \"w\") returned NULL on a fresh image with no /tmp)"

if [ ! -f "$MNT/etc/master.passwd" ]; then
    echo "+ no $MNT/etc/master.passwd -- provisioning real root account (password: darwin)"
    HASHTOOL="$(mktemp -t hash_password_host)"
    clang -Wall -Wextra -I tools/pwtool -o "$HASHTOOL" \
        tools/pwtool/hash_password_host.c tools/libinfo_custom/crypt_des.c
    ROOTHASH=$("$HASHTOOL" darwin)
    rm -f "$HASHTOOL"
    if [ -z "$ROOTHASH" ]; then
        echo "error: root password hash generation failed" >&2
        exit 1
    fi
    printf 'root:%s:0:0::0:0::/:/bin/sh\n' "$ROOTHASH" > "$MNT/etc/master.passwd"
    printf 'root:*:0:\n' > "$MNT/etc/group"
    chmod 600 "$MNT/etc/master.passwd"
    chmod 644 "$MNT/etc/group"
    echo "+ wrote $MNT/etc/master.passwd and $MNT/etc/group (root:darwin, uid/gid 0)"
else
    echo "+ $MNT/etc/master.passwd already exists -- leaving accounts as-is"
fi

diskutil unmount "$MNT"
hdiutil detach "$DISK"
trap - EXIT

echo "done -- $SD_IMAGE's /sbin/launchd, /sbin/pwtool, and /etc are up to date"
