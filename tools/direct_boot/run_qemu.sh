#!/bin/bash
# Rebuilds the flat XNU image + EL3->EL2->EL1 trampoline (see build.py) and
# boots it directly in QEMU, bypassing u-boot entirely.
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
#
# Last known state (2026-08-07): boots through our trampoline into real XNU
# kernel code at EL1 with MMU enabled (confirmed via symbol resolution -
# see symlookup.py), but hits an early panic (_panic -> handle_debugger_trap
# -> PEHaltRestartInternal -> halt_all_cpus) before debug_buf/panic_info are
# populated, so no panic message text is recoverable from memory yet.

set -e
cd "$(dirname "$0")/../.."

python3 tools/direct_boot/build.py

qemu-system-aarch64 -M raspi3b -m 1024 -display none \
  -serial null -serial stdio \
  -device loader,file=qemu/direct_boot/xnu_image.bin,addr=0x1004000 \
  -device loader,file=qemu/direct_boot/trampoline.bin,addr=0x80000 \
  -device loader,addr=0x80000,cpu-num=0 \
  -monitor unix:qemu/mon.sock,server,nowait
