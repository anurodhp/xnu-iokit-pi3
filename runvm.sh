#!/bin/bash
# Boots the current from-source xnu-7195 build (branch bcm2837-bringup) via
# qemu-zhuowei's raspi3 loader - the proven boot-verification path (see
# project memory "FROM-SOURCE BOOT MILESTONE REACHED"). Do NOT swap in plain
# system qemu-system-aarch64 - it lacks qemu-zhuowei's local
# secondary-core-parking patch and will not boot correctly.
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
# Audio bring-up boot-args (audio=1, pwm-sim=1, ...) are listed in
# docs/audio-bringup.md; pass them through EXTRA_BOOT_ARGS.
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
# `vm_compressor=0` (VM_PAGER_NOT_CONFIGURED), added 2026-09-14: real
# gap found via the FIRST real launchd.macho boot (previous PID-1 was a
# custom static binary that never triggered this). Default
# vm_compressor_mode is VM_PAGER_COMPRESSOR_WITH_SWAP, which spawns a
# real background kernel thread (vm_swapfile_create_thread,
# osfmk/vm/vm_compressor_backing_store.c) that repeatedly tries to
# create a real swap file on "/" -- impossible on a fresh SD image (no
# /private/var/vm), so it retries forever ("vm_swap_create_file failed
# @ N secs" on console, real not cosmetic). Confirmed via a live QEMU
# monitor register sample (`info registers` + `x` on the frame pointer,
# resolved against the unstripped kernel's own nm table) that this
# background thread's real filesystem-creation attempt and launchd's
# own foreground `mount("devfs", "/dev", ...)` call race on the root
# mount's real per-mount locks (vnode_iterate -> IOLockLock, found
# spinning) -- the boot hung indefinitely right after "remounted /
# read-write" in the large majority of real test runs.
#
# `vm_compressor=2` (VM_PAGER_COMPRESSOR_NO_SWAP) was tried FIRST and
# does NOT fix this -- a real, confirmed XNU naming quirk
# (osfmk/vm/vm_pageout.c's real switch on vm_compressor_mode): NO_SWAP
# only means `vm_config.swap_is_active` stays FALSE (swap isn't used
# for paging), but `vm_config.swap_is_present` is set TRUE for BOTH
# WITH_SWAP and NO_SWAP, and vm_compressor_swap_init() -- the function
# that spawns vm_swapfile_create_thread -- is gated on
# VM_CONFIG_SWAP_IS_PRESENT, not swap_is_active. Only
# VM_PAGER_NOT_CONFIGURED (0x0, "no compressor or swap configured")
# leaves swap_is_present FALSE (the bzero'd struct default, untouched
# by that mode's real case in the switch). Disables in-RAM compression
# too, not just swap -- acceptable for this 1GB QEMU target's real
# usage (a single test boot, not a production workload), same real
# reasoning as choosing no swap partition at all.
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
#
# EXPECT en0's MAC to read 40:54:00:12:34:5X, NOT 52:54:00:12:34:5X. That is
# QEMU's own quirk, not a bug in this port, and it has already cost one
# investigation (DAR-278). qemu_macaddr_default_if_unset() hands usb-net
# 52:54:00:12:34:5X, but hw/usb/dev-network.c's usb_net_realize() then builds
# the CDC-ECM iMACAddress string descriptor with the first octet HARDCODED:
#
#     snprintf(s->usbstring_mac, sizeof(s->usbstring_mac),
#              "%02x%02x%02x%02x%02x%02x",
#              0x40,                      <-- literal, not macaddr.a[0]
#              s->conf.macaddr.a[1], ... a[5]);
#
# so only octet 0 differs and the other five match - which reads exactly like
# a first-byte corruption bug in the host controller's control-transfer data
# stage. It is not. Measured on both sides in one boot (2026-09-09): QEMU put
# 1a 03 34 00 30 00 35 00 34 00 30 00 30 00 31 00 32 00 33 00 34 00 35 00 37 00
# ("405400123457" in UTF-16LE) on the wire, and the guest read it back byte
# for byte. The RNDIS configuration reports the real 52:54:... via
# OID_802_3_CURRENT_ADDRESS; only the CDC-ECM string is affected. Nothing
# filters on it either - usbnet_receive() has no MAC filter and SLIRP learns
# the guest's address from its ARP/DHCP traffic - so DHCP is unaffected.
#
# QEMU_SMP=N (DAR-455, N = 2..4): boot with cores 1..N-1 released. Adds
# qemu-zhuowei's opt-in `xnu-smp=on` raspi3 machine property (cores 1-3
# start in the spin-table stub at EL2, as the real Pi 3 firmware leaves
# them, instead of being halted) and the kernel boot-arg cpus=N (on
# BCM2837 the kernel keeps only the boot CPU unless cpus=N is given).
# Unset (the default): both off, every boot exactly as before. raspi3
# always has four vCPUs, so -smp needs no change.
#
# QEMU_DTB overrides the device tree (default devicetrees/rpi3.dtb), e.g.
# to boot an older tree against a newer kernel.
#
# QEMU_EXTRA_ARGS is appended to the QEMU command line verbatim, e.g.
# QEMU_EXTRA_ARGS="-accel tcg,thread=single" to compare MTTCG (the default
# on this host) with round-robin TCG.
set -e
cd "$(dirname "$0")"
mkdir -p qemu

QEMU_MACHINE="raspi3"
SMP_BOOT_ARGS=""
if [ -n "${QEMU_SMP:-}" ]; then
  case "$QEMU_SMP" in
    2|3|4) ;;
    *) echo "error: QEMU_SMP must be 2, 3 or 4 (got '$QEMU_SMP')" >&2; exit 1 ;;
  esac
  QEMU_MACHINE="raspi3,xnu-smp=on"
  SMP_BOOT_ARGS="cpus=$QEMU_SMP"
fi

SD_IMAGE="${SD_IMAGE:-qemu/hfs_root_test.img}"
ROOT_DEV="${ROOT_DEV:-disk0s2}"
# Headless by default. runvm_hdmi.sh sets this to a real window (and adds
# hdmi_console=1) to show the HDMI framebuffer; serial stays on stdio
# either way.
QEMU_DISPLAY="${QEMU_DISPLAY:-none}"

if [ -z "${USB_DEVICES+set}" ]; then
  USB_DEVICES="-device usb-hub,id=lan9514hub,bus=usb-bus.0,port=1 -netdev user,id=usbnet0 -device usb-net,netdev=usbnet0,bus=usb-bus.0,port=1.1"
fi

# EXTRA_USB_DEVICES is appended to USB_DEVICES (default or not), so a
# caller can add a device without restating the LAN9514 stand-in above.
# runvm_hdmi.sh uses it for a USB keyboard (DAR-245).
USB_DEVICES="$USB_DEVICES${EXTRA_USB_DEVICES:+ $EXTRA_USB_DEVICES}"

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
# BT_SERIAL: what the PL011 (serial_hd(0), the Bluetooth UART on a real Pi 3)
# is wired to; the console mini-UART is serial_hd(1). Default null. For the
# Bluetooth driver test, tools/bt_h4_responder.py listens on a Unix socket:
#   BT_SERIAL=unix:qemu/bt.sock EXTRA_BOOT_ARGS=bt=1 ./runvm.sh
BT_SERIAL="${BT_SERIAL:-null}"
exec ./third_party/qemu-zhuowei/aarch64-softmmu/qemu-system-aarch64 -M "$QEMU_MACHINE" -m 1024 -display "$QEMU_DISPLAY" \
  -serial "$BT_SERIAL" -serial stdio \
  -kernel build/xnu-latest-build/out/kernel.development.bcm2837-7195.unstripped \
  -dtb "${QEMU_DTB:-devicetrees/rpi3.dtb}" \
  -drive "file=$SD_IMAGE,if=sd,format=raw" \
  $USB_DEVICES \
  -append "debug=0x8 kextlog=0xfff -noprogress -v -enable_kprintf_spam rd=$ROOT_DEV serial=3 cpu-factor=10 vm_compressor=0 $SMP_BOOT_ARGS $EXTRA_BOOT_ARGS" \
  -monitor unix:qemu/mon.sock,server,nowait \
  $QEMU_EXTRA_ARGS
