#!/bin/bash
# Boots the freestanding init binary via mockfs in QEMU (zhuowei fork).
#
# Requires a kernel rebuilt with mockfs enabled (see README.md) -- the
# kernel checked into precompiled/ as of this writing does NOT have it,
# this will just boot to idle without ever reaching load_init_program's
# /sbin/launchd exec.
set -e
cd "$(dirname "$0")/../.."

python3 tools/init_binary/build.py

qemu-system-aarch64 -M raspi3 \
  -kernel precompiled/kernel.development.bcm2837.unstripped \
  -dtb devicetrees/rpi3.dtb \
  -initrd tools/init_binary/init.macho \
  -append "rd=md0" \
  -serial stdio
