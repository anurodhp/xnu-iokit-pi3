#!/bin/bash
# Boots the current from-source xnu-7195 build (branch bcm2837-bringup) via
# qemu-zhuowei's raspi3 loader - the proven boot-verification path (see
# project memory "FROM-SOURCE BOOT MILESTONE REACHED"). Do NOT swap in plain
# system qemu-system-aarch64 or tools/direct_boot - both lack qemu-zhuowei's
# local secondary-core-parking patch and will not boot correctly.
#
# EXPECTED BEHAVIOR: console prints a couple of early boot lines ("xnu",
# mmap/jit debug lines) and then goes silent forever - that silence is
# success, not a hang. There's no userland yet (mockfs shell is still
# blocked, see project memory), so XNU reaches _processor_idle and just
# spins with no further console output. QEMU stays pegged near 100% CPU
# while idling (TCG executing the wfi/idle loop) - a process that drops to
# ~0% CPU is actually stuck, not idle.
#
# To confirm it's genuinely idling (not hung) from another shell:
#   echo "info registers" | nc -U qemu/mon.sock
# PC should land in/near _processor_idle. Re-run to see it still moving
# within the idle loop.
set -e
cd "$(dirname "$0")"
mkdir -p qemu

./third_party/qemu-zhuowei/aarch64-softmmu/qemu-system-aarch64 -M raspi3 -m 1024 -display none \
  -serial null -serial stdio \
  -kernel build/xnu-latest-build/out/kernel.development.bcm2837-7195.unstripped \
  -dtb devicetrees/rpi3.dtb \
  -append "debug=0x8 kextlog=0xfff -noprogress -v" \
  -monitor unix:qemu/mon.sock,server,nowait

