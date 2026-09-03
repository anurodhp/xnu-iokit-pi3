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
#
# EXTRA_BOOT_ARGS appends to the baked-in boot-args below, for kernel
# debug switches that should NOT be on by default. Added 2026-09-03
# (DAR-222) so `-user_fault_debug` -- osfmk/kern/exception.c's opt-in
# report of every userland exception's real type/PC/FAR/ESR/backtrace,
# the thing that finally located that crash -- is reachable from this
# project's own scripts instead of needing a hand-edit here, e.g.:
#   EXTRA_BOOT_ARGS=-user_fault_debug ./runvm.sh
#   EXTRA_BOOT_ARGS=-user_fault_debug tools/boot_test.py --login ...
# (tools/boot_test.py passes its environment straight through.)
#
# `cpu-factor=10`, added 2026-08-13: real ARM64 interrupt delivery landed
# this same day (docs/roadmap.md Phase 3), which for the first time put
# real sustained interrupt-driven I/O load (multiple sequential real SD
# reads via the shell's now-real ls/cd/pwd, Phase 3.5a) through a code path
# that had never been exercised this heavily before. That surfaced a real,
# ~40%-reproducible panic under this exact QEMU (TCG, no HVF acceleration)
# setup: `osfmk/arm/machine_routines_common.c`'s INTERRUPT_MASKED_DEBUG
# watchdog (`Processing of an interrupt ... took N ns (timeout = 35498666
# ns)`, always flagging `_rtclock_intr`, XNU's own periodic timer tick).
# Root cause is QEMU/TCG's well-known wall-clock-vs-guest-instruction-count
# unreliability under host scheduling contention, not a guest kernel bug -
# confirmed via multiple back-to-back identical test runs producing
# different pass/fail outcomes with byte-identical guest state, and the
# panic's own handler address/vector being identical every time it fired
# (a real infinite loop or corruption would not look like this).
# `cpu-factor` is a real, first-class Apple pexpert boot-arg
# (`pexpert/gen/pe_gen.c`) that scales this exact watchdog's timeout via
# `debug_cpu_performance_degradation_factor` - normally auto-set to 10 for
# real FPGA prototype hardware (`target-is-fpga` in the device tree), a
# strictly analogous "known slow/jittery, don't false-panic" situation to
# QEMU/TCG. Deliberately QEMU-only: real hardware's boot chain (u-boot's
# `bootxnu`, not this script) is untouched, since real silicon's timing
# should be consistent enough that this watchdog stays diagnostically
# meaningful there.
#
# USB topology, added 2026-08-21 alongside the DWC2 backport into
# third_party/qemu-zhuowei (see that fork's "hw/usb: backport DWC2 host
# controller from QEMU 5.2" commit): this fork was based on QEMU 2.12.91,
# which models no USB controller for -M raspi3 at all, so 0x3f980000 read
# back as unassigned memory and no USB driver could be tested. The two
# -device lines below reproduce the Pi 3's actual wiring - the SMSC LAN9514
# is a combo hub+Ethernet chip soldered to the single root port, with its
# Ethernet function behind one of its own internal hub ports, so a bare
# device on the root port would NOT exercise the code path real hardware
# needs (docs/historic/usb-support-plan.md's "hub-in-the-path" note).
#
# QEMU has no LAN9514 model, so usb-net (an RNDIS device) stands in for the
# Ethernet function. That is deliberate and its limit is known: it exercises
# DWC2 transfers, device enumeration and hub port power/reset/attach for
# real, but NOT the SMSC9514-specific register protocol, which can only be
# verified on real hardware. Set USB_DEVICES= (empty) to boot with a bare
# root port and no devices attached.
set -e
cd "$(dirname "$0")"
mkdir -p qemu

SD_IMAGE="${SD_IMAGE:-qemu/hfs_root_test.img}"
ROOT_DEV="${ROOT_DEV:-disk0s2}"

if [ -z "${USB_DEVICES+set}" ]; then
  USB_DEVICES="-device usb-hub,id=lan9514hub,bus=usb-bus.0,port=1 -netdev user,id=usbnet0 -device usb-net,netdev=usbnet0,bus=usb-bus.0,port=1.1"
fi

if [ ! -f "$SD_IMAGE" ]; then
  echo "error: SD card image not found: $SD_IMAGE" >&2
  exit 1
fi

# `exec` so this script's shell is REPLACED by QEMU rather than lingering as
# its parent. Without it a harness that signals this script's process group
# can reap the shell while QEMU survives as an orphan; several such orphans
# accumulated during the 2026-08-21 USB work and their combined TCG load
# starved a later boot badly enough to look like a boot regression (see
# tools/boot_test.py's header on why concurrent QEMUs produce exactly that
# false negative).
exec ./third_party/qemu-zhuowei/aarch64-softmmu/qemu-system-aarch64 -M raspi3 -m 1024 -display none \
  -serial null -serial stdio \
  -kernel build/xnu-latest-build/out/kernel.development.bcm2837-7195.unstripped \
  -dtb devicetrees/rpi3.dtb \
  -drive "file=$SD_IMAGE,if=sd,format=raw" \
  $USB_DEVICES \
  -append "debug=0x8 kextlog=0xfff -noprogress -v -enable_kprintf_spam rd=$ROOT_DEV serial=3 cpu-factor=10 $EXTRA_BOOT_ARGS" \
  -monitor unix:qemu/mon.sock,server,nowait
