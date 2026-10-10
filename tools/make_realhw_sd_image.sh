#!/bin/bash
# Creates a fresh 2-partition (FAT32 boot + HFS+ root) SD card image for
# REAL RASPBERRY PI 3 HARDWARE, using the CURRENT from-source kernel
# (build/xnu-latest-build/out/kernel.development.bcm2837-7195.unstripped)
# instead of the frozen pre-session snapshot in sdcard/. Otherwise
# byte-for-byte the same recipe as tools/make_sd_image.sh (which builds
# qemu/hfs_root_test.img for QEMU) -- same partition layout, same real
# sdcard/ boot firmware (bootcode.bin/start.elf/fixup.dat/config.txt/
# rpi3.dtb/u-boot.bin, copied there even by the QEMU script purely for
# real-hardware parity, since QEMU itself boots via -kernel/-dtb and
# never reads them), same tools/init_binary/inject_into_sd_image.sh root
# provisioning. The ONLY functional difference between the two scripts
# is which kernel file lands in the boot partition -- kept as its own
# separate script rather than a flag on make_sd_image.sh because writing
# the wrong kernel onto a real physical SD card is an easy, SILENT
# mistake (no error, it just boots stale code) -- a distinctly-named
# script with the choice baked into its filename is safer than a
# same-named flag you could forget to pass.
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
# is combined with real Pi 3 silicon. sdcard/docs/sdcard-boot-notes.md's own "Known risk
# points" (real SDHCI serialization/error-recovery/clock-speed/power-ramp/
# memory-barriers/GPIO-pinmux gaps, docs/backlog.md P0 items 1-6) are all
# real-hardware-only risks QEMU cannot exercise -- this image is what
# finally puts them in scope for real, not something already verified.
#
# Usage: tools/make_realhw_sd_image.sh [OUTPUT_IMAGE] [SIZE_MB]
#   OUTPUT_IMAGE: defaults to qemu/realhw_root.img
#   SIZE_MB:      defaults to 3072 (3 GB) -- room for the
#                 provisioned root (commands, GNUstep, X, Florence) while clearing the 1024 MiB SDHC/SDSC
#                 threshold with a safe margin, and small enough to write
#                 quickly regardless of physical card size. A nominal
#                 "4 GB" card's real usable capacity is normally a bit
#                 under 4096 MiB (manufacturer GB = 1,000,000,000 bytes,
#                 not the binary MiB this script's mkfile/dd use, plus
#                 some reserved/spare sectors) -- push the size up only if
#                 the provisioned root actually needs more room, and if
#                 so double-check your card's real byte capacity with
#                 `diskutil info` first (3800 and even 3500 both proved
#                 too large a margin against one real "4GB" card,
#                 confirmed 2026-08-21).
set -e
cd "$(dirname "$0")/.."
mkdir -p qemu

OUT="${1:-qemu/realhw_root.img}"
SIZE_MB="${2:-3072}"

KERNEL_SRC="build/xnu-latest-build/out/kernel.development.bcm2837-7195.unstripped"
if [ ! -f "$KERNEL_SRC" ]; then
    echo "error: $KERNEL_SRC not found -- run tools/build_xnu_7195.sh and copy its" >&2
    echo "  output there first (see CLAUDE.md's Build commands section)" >&2
    exit 1
fi

# The dtb has the SAME staleness trap the kernel comment above already
# guards against, and until 2026-08-21 this script fell into it: it copied
# sdcard/rpi3.dtb, a frozen snapshot (last regenerated 2026-08-13, predating
# the "usb" node added for Phase 6a) instead of the tree that actually
# matches the current source. Building fresh here rather than trusting
# devicetrees/rpi3-realhw.dtb to already be up to date, for the same
# reason: a stale committed .dtb is a silent mistake, a freshly regenerated
# one cannot be.
DTB_SRC="devicetrees/rpi3-realhw.dtb"
echo "+ regenerating $DTB_SRC from current devicetrees/ source"
make -C devicetrees rpi3-realhw.dtb
if [ ! -f "$DTB_SRC" ]; then
    echo "error: $DTB_SRC not found after 'make -C devicetrees'" >&2
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
echo "  sdcard/ (bootcode.bin/start.elf/fixup.dat/config.txt/u-boot.bin are"
echo "  unmodified VC/u-boot firmware, not this project's source, so the frozen"
echo "  copies are fine), but the CURRENT from-source kernel and dtb"
cp sdcard/bootcode.bin sdcard/start.elf sdcard/fixup.dat sdcard/config.txt \
   sdcard/u-boot.bin "$BOOT_MNT/"
cp "$KERNEL_SRC" "$BOOT_MNT/kernel.development.bcm2837.unstripped"
echo "+ wrote $(wc -c < "$KERNEL_SRC") bytes to $BOOT_MNT/kernel.development.bcm2837.unstripped"
echo "  (current from-source build, renamed to match u-boot's baked-in"
echo "  CONFIG_BOOTCOMMAND fatload target -- see this script's own header)"
cp "$DTB_SRC" "$BOOT_MNT/rpi3.dtb"
echo "+ wrote $(wc -c < "$DTB_SRC") bytes to $BOOT_MNT/rpi3.dtb"
echo "  (freshly built from $DTB_SRC, renamed to match u-boot's fatload target"
echo "  -- same rename convention as the kernel above, NOT sdcard/'s frozen copy)"

# Extra kernel boot-args. u-boot's bootcmd loads uEnv.txt from FAT partition
# 0:1 if present, `env import -t`s it and appends ${extra_bootargs}
# (anurodhp/u-boot configs/rpi3_xnu_defconfig), so this needs no u-boot
# rebuild and never drives u-boot over serial.
# Default: audio=1 cpus=4. audio=1 starts the DMA, PWM and audio-engine drivers for the 3.5mm jack (DARW-310;
# docs/audio-bringup.md; without it they stay off and never touch the hardware; add audio-test=<s> for a
# kernel-played tone). cpus=4 starts cores 1-3 (DAR-455; verified on the real Pi 2026-10-02; drop it for a single-core boot).
# No fb args: the kernel framebuffer is the monitor's native size, asked of the
# VideoCore firmware (Get Physical W/H, tag 0x00040003; third_party/xnu-7195
# pexpert/arm/pe_init.c bcm2837_video_init), capped to 1920x1080 above the
# gpu_mem-backed bounds, and 640x480 where the firmware reports nothing (QEMU).
# fb_width/fb_height boot-args (clamped to 320..1920 x 200..1200) still force a
# size, e.g. when the monitor's EDID is wrong. Override:
#   REALHW_EXTRA_BOOTARGS="-user_fault_debug fb_width=1280 fb_height=720" tools/make_realhw_sd_image.sh out.img
# or REALHW_EXTRA_BOOTARGS= (set but empty) for no uEnv.txt at all.
REALHW_EXTRA_BOOTARGS="${REALHW_EXTRA_BOOTARGS-audio=1 cpus=4}"
if [ -n "$REALHW_EXTRA_BOOTARGS" ]; then
    printf 'extra_bootargs=%s\n' "$REALHW_EXTRA_BOOTARGS" > "$BOOT_MNT/uEnv.txt"
    echo "+ wrote $BOOT_MNT/uEnv.txt: extra_bootargs=$REALHW_EXTRA_BOOTARGS"
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
echo "  (SKIP_SMOKETESTS=1: real hardware doesn't get the *_smoketest.macho"
echo "  QEMU dev-diagnostic binaries -- see inject_into_sd_image.sh's own header)"
SKIP_SMOKETESTS=1 tools/init_binary/inject_into_sd_image.sh "$OUT" 2

echo "done -- $OUT is a fresh, fully provisioned, real-hardware-bootable SD image"
echo ""
echo "To write it to a real SD card:"
echo "  1. diskutil list                     # find the real card's disk identifier"
echo "  2. diskutil unmountDisk /dev/diskN    # replace N with the real card's number"
echo "  3. sudo dd if=$OUT of=/dev/rdiskN bs=4m   # rdiskN (raw), NOT diskN -- much faster"
echo "  DOUBLE-CHECK diskN is the SD card, not your boot disk, before running dd."
