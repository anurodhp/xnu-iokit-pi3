#!/bin/bash
# Creates a fresh 2-partition (FAT32 boot + HFS+ root) SD card image for
# REAL RASPBERRY PI 3 HARDWARE, using the CURRENT from-source kernel
# (build/xnu-latest-build/out/kernel.development.bcm2837-7195.unstripped)
# instead of the frozen pre-session snapshot in sdcard/. Same recipe as
# tools/make_sd_image.sh (which builds qemu/hfs_root_test.img for QEMU),
# deliberately kept as its own separate script rather than a flag on that
# one -- the two have different, easy-to-confuse defaults (real vs QEMU
# device tree, real vs frozen kernel, real-hardware boot firmware), and
# writing the wrong one to a real SD card is a real footgun worth a
# distinct script name to avoid.
#
# 4 GB (see SIZE_MB default below) is load-bearing, not arbitrary --
# below it puts the SD card into SDSC (CSD v1) mode, which BCM2837SDHCI
# deliberately doesn't implement (SDHC/SDXC only) -- see docs/roadmap.md
# and tools/make_sd_image.sh's own header for the same reasoning.
#
# The kernel is copied under the filename `u-boot.bin`'s baked-in
# CONFIG_BOOTCOMMAND actually fatload's (kernel.development.bcm2837.
# unstripped, no "-7195" suffix) -- renaming on copy rather than
# rebuilding u-boot just to change one string.
#
# Real, honest risk this project hasn't closed yet: this is the first
# time the CURRENT from-source kernel (only ever booted on QEMU so far)
# is combined with real Pi 3 silicon. sdcard/README.md's own "Known risk
# points" (real SDHCI serialization/error-recovery/clock-speed/power-ramp/
# memory-barriers/GPIO-pinmux gaps, docs/backlog.md P0 items 1-6) are all
# real-hardware-only risks QEMU cannot exercise -- this image is what
# finally puts them in scope for real, not something already verified.
#
# Usage: tools/make_realhw_sd_image.sh [OUTPUT_IMAGE] [SIZE_MB]
#   OUTPUT_IMAGE: defaults to qemu/realhw_root.img
#   SIZE_MB:      defaults to 3500 -- sized for a genuine "4 GB" SD card: a
#                 nominal 4GB card's real usable capacity is normally a bit
#                 under 4096 MiB (manufacturer GB = 1,000,000,000 bytes,
#                 not the binary MiB this script's mkfile/dd use, plus
#                 some reserved/spare sectors) -- a literal 4096 MiB image
#                 can be too large to `dd` onto a real "4GB" card (3800
#                 still wasn't a large enough margin against this specific
#                 card, confirmed 2026-08-21). 3500 is a wider margin,
#                 still comfortably above the 2048 MiB SDHC threshold
#                 above. Double-check your actual card's real byte
#                 capacity with `diskutil info` if you want to push it
#                 closer to 4096.
set -e
cd "$(dirname "$0")/.."
mkdir -p qemu

OUT="${1:-qemu/realhw_root.img}"
SIZE_MB="${2:-3500}"

KERNEL_SRC="build/xnu-latest-build/out/kernel.development.bcm2837-7195.unstripped"
if [ ! -f "$KERNEL_SRC" ]; then
    echo "error: $KERNEL_SRC not found -- run tools/build_xnu_7195.sh and copy its" >&2
    echo "  output there first (see CLAUDE.md's Build commands section)" >&2
    exit 1
fi
if ! file "$KERNEL_SRC" | grep -q "Mach-O"; then
    echo "error: $KERNEL_SRC doesn't look like a real Mach-O -- refusing to ship it" >&2
    exit 1
fi

if [ -e "$OUT" ]; then
    echo "error: $OUT already exists -- remove it first (this script never overwrites)" >&2
    exit 1
fi

echo "+ creating ${SIZE_MB} MB flat raw image at $OUT"
mkfile -n "${SIZE_MB}m" "$OUT"

DISK=$(hdiutil attach -nomount -imagekey diskimage-class=CRawDiskImage "$OUT" | awk 'NR==1{print $1}')
if [ -z "$DISK" ]; then
    echo "error: hdiutil attach did not report a device node" >&2
    rm -f "$OUT"
    exit 1
fi
echo "+ attached as $DISK"

cleanup() {
    diskutil unmountDisk "$DISK" >/dev/null 2>&1 || true
    hdiutil detach "$DISK" >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "+ partitioning: MBR, partition 1 = 64M FAT32 boot, partition 2 = HFS+ root (rest)"
diskutil partitionDisk "$DISK" MBR \
    "MS-DOS FAT32" BOOT 64M \
    "JHFS+" ROOT R

BOOT_SLICE="${DISK}s1"
ROOT_SLICE="${DISK}s2"

BOOT_MNT=$(diskutil info "$BOOT_SLICE" | awk -F': +' '/Mount Point/{print $2}')
ROOT_MNT=$(diskutil info "$ROOT_SLICE" | awk -F': +' '/Mount Point/{print $2}')
if [ -z "$BOOT_MNT" ] || [ -z "$ROOT_MNT" ]; then
    echo "error: could not determine mount points after partitioning" >&2
    exit 1
fi

echo "+ populating boot partition ($BOOT_MNT) -- real hardware boot firmware from"
echo "  sdcard/, but the CURRENT from-source kernel, not sdcard/'s frozen snapshot"
cp sdcard/bootcode.bin sdcard/start.elf sdcard/fixup.dat sdcard/config.txt \
   sdcard/rpi3.dtb sdcard/u-boot.bin "$BOOT_MNT/"
cp "$KERNEL_SRC" "$BOOT_MNT/kernel.development.bcm2837.unstripped"
echo "+ wrote $(wc -c < "$KERNEL_SRC") bytes to $BOOT_MNT/kernel.development.bcm2837.unstripped"
echo "  (current from-source build, renamed to match u-boot's baked-in"
echo "  CONFIG_BOOTCOMMAND fatload target -- see this script's own header)"

echo "+ creating base root directory structure on ($ROOT_MNT)"
mkdir -p "$ROOT_MNT/sbin" "$ROOT_MNT/bin" "$ROOT_MNT/usr/bin" \
         "$ROOT_MNT/dev" "$ROOT_MNT/etc" "$ROOT_MNT/tmp" \
         "$ROOT_MNT/var/db/timezone/zoneinfo"

diskutil unmount "$BOOT_MNT"
diskutil unmount "$ROOT_MNT"
hdiutil detach "$DISK"
trap - EXIT

echo "+ base image ready: $OUT (disk0s1 = FAT32 boot, disk0s2 = HFS+ root)"
echo "+ provisioning userland (launchd/pwtool/bin/*, timezone data, root account)"
tools/init_binary/inject_into_sd_image.sh "$OUT" 2

echo "done -- $OUT is a fresh, fully provisioned, real-hardware-bootable SD image"
echo ""
echo "To write it to a real SD card:"
echo "  1. diskutil list                     # find the real card's disk identifier"
echo "  2. diskutil unmountDisk /dev/diskN    # replace N with the real card's number"
echo "  3. sudo dd if=$OUT of=/dev/rdiskN bs=4m   # rdiskN (raw), NOT diskN -- much faster"
echo "  DOUBLE-CHECK diskN is the SD card, not your boot disk, before running dd."
