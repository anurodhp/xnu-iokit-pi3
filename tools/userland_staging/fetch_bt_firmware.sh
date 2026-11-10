#!/bin/bash
# Downloads the BCM43430A1 Bluetooth patch (patchram .hcd) the Pi 3 Model B
# needs (docs/bluetooth-plan.md) into libc_build/bt_firmware/, at a pinned
# commit of RPi-Distro/bluez-firmware (what Raspberry Pi OS ships), and
# checks its SHA-256. It is a Cypress binary under the Cypress Wireless
# Connectivity Devices Driver EULA, which allows distributing it in object
# code form for use with Cypress chips (the Pi 3B's CYW43438), so the image
# ships it, with that licence beside it: the repo's debian/copyright, at the
# same commit, which names debian/firmware/broadcom/* under it. Neither is
# committed here (libc_build/ is gitignored). inject_into_sd_image.sh runs
# this script and installs both to /usr/share/firmware/bluetooth/, where
# /usr/sbin/bt looks by default.
set -euo pipefail
cd "$(dirname "$0")"

COMMIT=cdf61dc691a49ff01a124752bd04194907f0f9cd
BASE="https://raw.githubusercontent.com/RPi-Distro/bluez-firmware/$COMMIT/debian"
OUT="libc_build/bt_firmware"
mkdir -p "$OUT"

fetch() { # url dest sha256
	if [ ! -f "$OUT/$2" ] || ! echo "$3  $OUT/$2" | shasum -a 256 -c - >/dev/null 2>&1; then
		curl -fsSL "$1" -o "$OUT/$2.tmp"
		mv "$OUT/$2.tmp" "$OUT/$2"
	fi
	echo "$3  $OUT/$2" | shasum -a 256 -c - || { echo "error: $2 checksum mismatch" >&2; exit 1; }
}

fetch "$BASE/firmware/broadcom/BCM43430A1.hcd" BCM43430A1.hcd c096ad4a5c3f06ed7d69eba246bf89ada9acba64a5b6f51b1e9c12f99bb1e1a7
fetch "$BASE/copyright"                        copyright      5f6acad870790f7e23150a30cab85c643f926266bf16e6a63acbde4257b957d4
echo "bluetooth firmware in $OUT"
