#!/bin/bash
# Builds the freestanding test binary using the aarch64-unknown-linux-gnu
# cross toolchain already installed in this project for u-boot builds
# (brew tap messense/macos-cross-toolchains) - real ELF ld/objcopy, unlike
# bare Apple clang+ld64 which can't link freestanding ELF objects at all.
set -ex
cd "$(dirname "$0")"

CC=aarch64-unknown-linux-gnu-gcc
OBJCOPY=aarch64-unknown-linux-gnu-objcopy

CFLAGS="-ffreestanding -fno-pic -fno-pie -nostdlib -static -O1 -g -Wall -Wextra -mgeneral-regs-only"

$CC $CFLAGS -c start.S -o start.o
$CC $CFLAGS -c uart.c -o uart.o
$CC $CFLAGS -c gpio.c -o gpio.o
$CC $CFLAGS -c sdhci.c -o sdhci.o
$CC $CFLAGS -c main.c -o main.o

$CC $CFLAGS -Wl,-T,linker.ld -o test.elf start.o uart.o gpio.o sdhci.o main.o
$OBJCOPY -O binary test.elf test.bin

echo "built test.bin ($(wc -c < test.bin) bytes), entry:"
aarch64-unknown-linux-gnu-readelf -h test.elf | grep Entry
