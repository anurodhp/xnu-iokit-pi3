#!/bin/bash
# Boots the freestanding init binary via mockfs in QEMU (zhuowei fork).
#
# Requires a kernel built with mockfs enabled (see README.md) -- as of
# 2026-08-08, third_party/xnu-7195 (branch bcm2837-bringup) has this: mockfs
# was added to config/MASTER.arm64.bcm2837's FILESYS_DEV/FILESYS_DEBUG (see
# project memory). This has NOT yet been re-tested against this specific
# init-binary/mockfs boot path though - the mockfs enable was verified to
# still reach steady-state idle, not verified to actually reach
# load_init_program's /sbin/launchd exec.
#
# Retired precompiled/kernel.development.bcm2837.unstripped (the NSG650
# binary, deleted 2026-08-08) in favor of the real xnu-7195 build output,
# overridable via a KERNEL_PATH env var (see tools/direct_boot/build.py for
# the same pattern). Uses qemu-zhuowei's own raspi3 XNU loader, not plain
# system qemu-system-aarch64 - see project memory ("FROM-SOURCE BOOT
# MILESTONE REACHED") for why that distinction matters.
set -e
cd "$(dirname "$0")/../.."

KERNEL_PATH="${KERNEL_PATH:-build/xnu-latest-build/out/kernel.development.bcm2837-7195.unstripped}"

python3 tools/init_binary/build.py

./third_party/qemu-zhuowei/aarch64-softmmu/qemu-system-aarch64 -M raspi3 -m 1024 -display none \
  -serial null -serial stdio \
  -kernel "$KERNEL_PATH" \
  -dtb devicetrees/rpi3.dtb \
  -initrd tools/init_binary/init.macho \
  -append "debug=0x8 kextlog=0xfff -noprogress -v rd=md0"
