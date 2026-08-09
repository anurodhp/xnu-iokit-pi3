#!/bin/bash
# Rebuilds the flat XNU image + EL3->EL2->EL1 trampoline (see build.py) and
# boots it directly in QEMU, bypassing u-boot entirely.
#
# STATUS (2026-08-08): superseded, NOT the boot-verification path - do not
# use this to judge whether an xnu-7195 build/fix is correct. It drives
# plain system `qemu-system-aarch64` via `-device loader`, which bypasses
# `hw/arm/boot.c` (and thus every XNU-specific fix that lives only in
# third_party/qemu-zhuowei's local, gitignored patches - most importantly
# the secondary-core-parking fix). Booting a known-good kernel through this
# path has produced a real-looking panic (vm_packing_verify_range) that
# doesn't occur when the same kernel is booted correctly - almost certainly
# unparked secondary cores racing into early boot, not a kernel bug. The
# actual verification command is qemu-zhuowei's own `-M raspi3` loader -
# see project memory ("FROM-SOURCE BOOT MILESTONE REACHED") for the exact
# invocation, including the `-append "debug=0x8 kextlog=0xfff -noprogress
# -v"` boot-args flag it needs (easy to forget - without it XNU boots with
# an empty command line, i.e. silently, indistinguishable from a hang
# without checking QEMU-monitor register state).
#
# Why this still exists (two real, narrow reasons, otherwise dead code):
#   1. It's the origin of the "-device loader, flat binary, no Mach-O/ELF
#      parsing" technique that tools/sdhci_baremetal/ and sdcard/'s real-
#      hardware notes still cite as precedent (load addresses, EL3-no-
#      transition assumption) - kept as the reference implementation of
#      that pattern, not because anything imports/calls into it.
#   2. Unlike qemu-zhuowei, it needs zero local QEMU patches (plain system
#      qemu-system-aarch64) - occasionally useful for isolated experiments
#      that only need "inject an arbitrary Mach-O/flat binary at a known
#      physical address and watch it run", decoupled from the raspi3
#      machine model entirely (e.g. the original EL3->EL1 trampoline work
#      itself, before qemu-zhuowei's XNU loader was trusted).
#
# Console output goes to this terminal (-serial stdio). A separate QEMU
# monitor socket is opened at qemu/mon.sock so you can inspect CPU state
# from another shell without needing QEMU's finicky Ctrl-A escape sequence
# (which doesn't reliably reach QEMU through macOS Terminal/zsh, since
# Ctrl-A is already bound to readline's "move to start of line"):
#
#   echo "info registers" | nc -U qemu/mon.sock
#   echo "x/10i <virtual addr>" | nc -U qemu/mon.sock   # disassemble
#   echo "x/2xg <virtual addr>" | nc -U qemu/mon.sock   # read memory

set -e
cd "$(dirname "$0")/../.."

python3 tools/direct_boot/build.py

qemu-system-aarch64 -M raspi3b -m 1024 -display none \
  -serial null -serial stdio \
  -device loader,file=qemu/direct_boot/xnu_image.bin,addr=0x1004000 \
  -device loader,file=qemu/direct_boot/trampoline.bin,addr=0x80000 \
  -device loader,addr=0x80000,cpu-num=0 \
  -monitor unix:qemu/mon.sock,server,nowait
