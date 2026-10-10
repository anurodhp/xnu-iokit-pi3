#!/bin/bash
# Creates a fresh 2-partition (FAT32 boot + HFS+ root) SD card image from
# scratch and fully provisions it -- the same recipe used by hand to build
# qemu/hfs_root_test.img (see docs/roadmap.md Phase 3.5b item 4): a flat
# raw file (no UDIF wrapper, so QEMU's -drive ...,format=raw can read it
# directly) attached via hdiutil's CRawDiskImage class and partitioned
# with diskutil partitionDisk.
#
# >1 GiB is load-bearing, not arbitrary: third_party/qemu-zhuowei/hw/sd/
# sd.c literally does `if (sd->size > 1 * GiB)` to decide whether the
# emulated SD card presents as SDHC (CSD v2) or SDSC (CSD v1) -- and
# BCM2837SDHCI.cpp (iokit/BCM2837SDHCI/BCM2837SDHCI.cpp, ~line 590-605)
# explicitly refuses SDSC cards outright (never implemented that CSD
# version's older capacity formula). [FIXED 2026-08-21] The previous
# 500 MB default silently violated this and built an unbootable image
# -- see docs/backlog.md. 1200 MB clears the 1024 MiB threshold with a
# safe margin while keeping attach/partition/detach time well below
# what the old 4096 MB default cost on every single test iteration.
#
# Usage: tools/make_sd_image.sh [OUTPUT_IMAGE] [SIZE_MB]
#   OUTPUT_IMAGE: defaults to qemu/hfs_root_test.img (runvm.sh's default)
#   SIZE_MB:      defaults to 3072 (3 GB: the GNUstep tree, X servers and Florence's ~130 MB
#                 no longer fit in 1200; never go below ~1100, see above)
set -e
cd "$(dirname "$0")/.."
mkdir -p qemu

OUT="${1:-qemu/hfs_root_test.img}"
SIZE_MB="${2:-3072}"

if [ "$SIZE_MB" -le 1024 ]; then
    echo "error: SIZE_MB=$SIZE_MB is at or below the 1024 MiB SDHC/SDSC threshold" >&2
    echo "  (third_party/qemu-zhuowei/hw/sd/sd.c: \`sd->size > 1 * GiB\`) -- this" >&2
    echo "  would silently build an image BCM2837SDHCI refuses at card-init time" >&2
    echo "  (SDSC unsupported, see docs/backlog.md) rather than failing loudly" >&2
    echo "  right now. Use at least 1100." >&2
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

echo "+ populating boot partition ($BOOT_MNT) from sdcard/ (real-hardware parity;"
echo "  QEMU itself boots via -kernel/-dtb and ignores this)"
cp sdcard/bootcode.bin sdcard/start.elf sdcard/fixup.dat sdcard/config.txt \
   sdcard/rpi3.dtb sdcard/u-boot.bin "$BOOT_MNT/"
# The Pi-boot kernel copy is only for real-hardware parity (QEMU ignores the boot partition); sdcard/ does not always
# have it, and its absence used to abort the whole image build (and leave the disk attached).
if [ -f sdcard/kernel.development.bcm2837.unstripped ]; then
  cp sdcard/kernel.development.bcm2837.unstripped "$BOOT_MNT/"
else
  echo "= sdcard/kernel.development.bcm2837.unstripped missing -- boot partition has no kernel copy (fine for QEMU)"
fi

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

echo "done -- $OUT is a fresh, fully provisioned, bootable SD image"
