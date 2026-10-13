#!/bin/bash
# Downloads the BCM43430A1 Bluetooth patch (patchram .hcd) the Pi 3 Model B
# needs (docs/bluetooth-plan.md) into libc_build/bt_firmware/, at a pinned
# commit of RPi-Distro/bluez-firmware (what Raspberry Pi OS ships), and
# checks its SHA-256. It is a Broadcom binary under its own redistribution
# licence, so it is fetched, never committed (libc_build/ is gitignored).
# inject_into_sd_image.sh installs it to /usr/share/firmware/bluetooth/,
# where /usr/sbin/bt looks by default.
set -euo pipefail
cd "$(dirname "$0")"

COMMIT=cdf61dc691a49ff01a124752bd04194907f0f9cd
URL="https://raw.githubusercontent.com/RPi-Distro/bluez-firmware/$COMMIT/debian/firmware/broadcom/BCM43430A1.hcd"
SHA=c096ad4a5c3f06ed7d69eba246bf89ada9acba64a5b6f51b1e9c12f99bb1e1a7
OUT="libc_build/bt_firmware"
F="$OUT/BCM43430A1.hcd"
mkdir -p "$OUT"
if [ ! -f "$F" ] || ! echo "$SHA  $F" | shasum -a 256 -c - >/dev/null 2>&1; then
	curl -fsSL "$URL" -o "$F.tmp"
	mv "$F.tmp" "$F"
fi
echo "$SHA  $F" | shasum -a 256 -c - || { echo "error: BCM43430A1.hcd checksum mismatch" >&2; exit 1; }
echo "bluetooth firmware in $OUT"
