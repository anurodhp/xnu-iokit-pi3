#!/bin/bash
# DAR-455: gathers the files for the real-Pi SMP experiment described in
# docs/smp-pi-experiment.md into build/smp-pi-experiment/ (gitignored).
#
# Inputs, both built from this checkout first:
#   build/xnu-latest-build/out/kernel.development.bcm2837-7195.unstripped
#       tools/build_xnu_7195.sh PYTHON=true   (xnu fork branch dar-455-smp)
#   devicetrees/rpi3-realhw.dtb
#       make -C devicetrees
# Output: the kernel and dtb under the names u-boot's bootcmd loads
# (configs/rpi3_xnu_defconfig: kernel.development.bcm2837.unstripped and
# rpi3.dtb from FAT partition 1), a uEnv.txt, and SHA256SUMS.
set -e
cd "$(dirname "$0")/.."

KERNEL=build/xnu-latest-build/out/kernel.development.bcm2837-7195.unstripped
DTB=devicetrees/rpi3-realhw.dtb
OUT=build/smp-pi-experiment

for f in "$KERNEL" "$DTB"; do
	[ -f "$f" ] || { echo "error: $f not built" >&2; exit 1; }
done
# The kernel must be the SMP-capable one, not an older build.
strings "$KERNEL" | grep -q 'releasing core %u at PA' || {
	echo "error: $KERNEL has no DAR-455 secondary bring-up" >&2; exit 1; }
# The dtb must list all four cores (state "waiting" for cpu1-3).
[ "$(strings -a "$DTB" | grep -c '^waiting$')" -eq 3 ] || {
	echo "error: $DTB does not list cpu1-3" >&2; exit 1; }

mkdir -p "$OUT"
cp "$KERNEL" "$OUT/kernel.development.bcm2837.unstripped"
cp "$DTB" "$OUT/rpi3.dtb"
printf 'extra_bootargs=fb_width=1280 fb_height=720 cpus=4\n' >| "$OUT/uEnv.txt"
(cd "$OUT" && shasum -a 256 kernel.development.bcm2837.unstripped rpi3.dtb uEnv.txt >| SHA256SUMS)
strings "$OUT/kernel.development.bcm2837.unstripped" | grep -m1 'Darwin Kernel Version'
cat "$OUT/SHA256SUMS"
echo "packaged -> $OUT"
