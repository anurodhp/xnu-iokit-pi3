#!/bin/bash
# Boots the current from-source xnu-7195 build (branch bcm2837-bringup) via
# qemu-zhuowei's raspi3 loader - the proven boot-verification path (see
# project memory "FROM-SOURCE BOOT MILESTONE REACHED"). Do NOT swap in plain
# system qemu-system-aarch64 or tools/direct_boot - both lack qemu-zhuowei's
# local secondary-core-parking patch and will not boot correctly.
#
# Boots real msdosfs root off an SD card image (qemu/fat32_test.dmg,
# attached via `-drive ...,if=sd`) instead of mockfs/initrd - `rd=disk0`
# selects it as the root device. `serial=3` is required for serial
# keyboard *input* to reach the interactive shell (init.macho); without it
# console output still appears but typed input is never polled. See
# project memory "INTERACTIVE SHELL MILESTONE REACHED" for the full
# writeup of this boot path.
#
# SD_IMAGE defaults to qemu/fat32_test.dmg; override with an env var if
# testing against a different image (e.g. qemu/sdcard_test.img).
set -e
cd "$(dirname "$0")"
mkdir -p qemu

SD_IMAGE="${SD_IMAGE:-qemu/fat32_test.dmg}"

if [ ! -f "$SD_IMAGE" ]; then
  echo "error: SD card image not found: $SD_IMAGE" >&2
  exit 1
fi

./third_party/qemu-zhuowei/aarch64-softmmu/qemu-system-aarch64 -M raspi3 -m 1024 -display none \
  -serial null -serial stdio \
  -kernel build/xnu-latest-build/out/kernel.development.bcm2837-7195.unstripped \
  -dtb devicetrees/rpi3.dtb \
  -drive "file=$SD_IMAGE,if=sd,format=raw" \
  -append "debug=0x8 kextlog=0xfff -noprogress -v rd=disk0 serial=3" \
  -monitor unix:qemu/mon.sock,server,nowait
