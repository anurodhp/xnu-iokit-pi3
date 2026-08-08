#!/bin/bash
# Creates a sparse >1GiB SD image (QEMU's hw/sd/sd.c sd_set_csd(): size <=
# 1*GiB gets CSD structure 0 / SDSC, which this driver deliberately doesn't
# support, matching iokit/BCM2837SDHCI's real documented scope - has to be
# bigger to get CSD structure 1 / SDHC) with block 0 = byte pattern
# buf[i]=(uint8_t)i (matches main.c's check_pattern()), then boots test.bin
# directly via QEMU's generic loader (same -device loader technique
# tools/direct_boot already proved works on this fork) with that image
# attached as the SD card (`-drive if=sd`, the real mechanism
# hw/arm/raspi.c:200's drive_get_next(IF_SD) uses).
set -ex
cd "$(dirname "$0")"

./build.sh

python3 -c "
import os
size = 2 * 1024**3 + 1024  # just over 2GiB
with open('sdcard.img', 'wb') as f:
    f.write(bytes((i & 0xFF for i in range(512))))
    f.truncate(size)
"

QEMU=../../third_party/qemu-zhuowei/aarch64-softmmu/qemu-system-aarch64

"$QEMU" -M raspi3 -m 1024 -display none \
  -serial stdio -serial null \
  -drive file=sdcard.img,if=sd,format=raw \
  -device loader,file=test.bin,addr=0x80000 \
  -device loader,addr=0x80000,cpu-num=0 \
  -monitor unix:mon.sock,server,nowait
