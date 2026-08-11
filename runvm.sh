#!/bin/bash
# Boots the current from-source xnu-7195 build (branch bcm2837-bringup) via
# qemu-zhuowei's raspi3 loader - the proven boot-verification path (see
# project memory "FROM-SOURCE BOOT MILESTONE REACHED"). Do NOT swap in plain
# system qemu-system-aarch64 or tools/direct_boot - both lack qemu-zhuowei's
# local secondary-core-parking patch and will not boot correctly.
#
# Boots a real, permission-bearing HFS+ root off a genuine two-partition SD
# card image (qemu/hfs_root_test.img: MBR partition 1 = FAT32 boot firmware,
# partition 2 = HFS+ root), attached via `-drive ...,if=sd`. `rd=disk0s2`
# selects the second MBR partition as the root device - resolved by
# IOFDiskPartitionScheme (iokit/Storage/IOFDiskPartitionScheme.cpp), wired
# in for the first time to close docs/roadmap.md Phase 3.5b. `serial=3` is
# required for serial keyboard *input* to reach the interactive shell
# (init.macho); without it console output still appears but typed input is
# never polled. See project memory "INTERACTIVE SHELL MILESTONE REACHED"
# for the shell-path writeup and "HFS root milestone" for this partition
# scheme + HFS root-mount writeup.
#
# SD_IMAGE/ROOT_DEV default to the two-partition HFS image; override with
# env vars to boot the older single-partition msdosfs image instead, e.g.:
#   SD_IMAGE=qemu/fat32_test.dmg ROOT_DEV=disk0 ./runvm.sh
set -e
cd "$(dirname "$0")"
mkdir -p qemu

SD_IMAGE="${SD_IMAGE:-qemu/hfs_root_test.img}"
ROOT_DEV="${ROOT_DEV:-disk0s2}"

if [ ! -f "$SD_IMAGE" ]; then
  echo "error: SD card image not found: $SD_IMAGE" >&2
  exit 1
fi

./third_party/qemu-zhuowei/aarch64-softmmu/qemu-system-aarch64 -M raspi3 -m 1024 -display none \
  -serial null -serial stdio \
  -kernel build/xnu-latest-build/out/kernel.development.bcm2837-7195.unstripped \
  -dtb devicetrees/rpi3.dtb \
  -drive "file=$SD_IMAGE,if=sd,format=raw" \
  -append "debug=0x8 kextlog=0xfff -noprogress -v rd=$ROOT_DEV serial=3" \
  -monitor unix:qemu/mon.sock,server,nowait
