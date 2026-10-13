#!/bin/bash
# Downloads the BCM43430A1 WiFi firmware the Pi 3 Model B needs
# (docs/wifi-plan.md) into libc_build/wifi_firmware/, at a pinned commit of
# RPi-Distro/firmware-nonfree (what Raspberry Pi OS ships), and checks
# SHA-256s. The files are Broadcom/Cypress binaries under their own
# redistribution licence, so they are fetched, never committed (libc_build/
# is gitignored). inject_into_sd_image.sh installs them to
# /usr/share/firmware/wifi/ under the names /usr/sbin/wifi expects.
#
# Raspberry Pi OS resolves brcmfmac43430-sdio.raspberrypi,3-model-b.* to
# exactly these three files (symlinks in debian/added-firmware/brcm/).
set -euo pipefail
cd "$(dirname "$0")"

COMMIT=3bab0f823f5b53150b76aab77093adef6655b920
BASE="https://raw.githubusercontent.com/RPi-Distro/firmware-nonfree/$COMMIT/debian/added-firmware"
OUT="libc_build/wifi_firmware"
mkdir -p "$OUT"

fetch() { # url dest sha256
	if [ ! -f "$OUT/$2" ] || ! echo "$3  $OUT/$2" | shasum -a 256 -c - >/dev/null 2>&1; then
		curl -fsSL "$1" -o "$OUT/$2.tmp"
		mv "$OUT/$2.tmp" "$OUT/$2"
	fi
	echo "$3  $OUT/$2" | shasum -a 256 -c - || { echo "error: $2 checksum mismatch" >&2; exit 1; }
}

fetch "$BASE/cypress/cyfmac43430-sdio.bin"      brcmfmac43430-sdio.bin      0717f8e798f3962230e76e9d840385ba127a38d31d6a55acd5c97cf53e4acc9d
fetch "$BASE/cypress/cyfmac43430-sdio.clm_blob" brcmfmac43430-sdio.clm_blob 3376b9c9b32d16bf762e21c7fafb665365070ae240d092498d0d1987c22022aa
fetch "$BASE/brcm/brcmfmac43430-sdio.txt"       brcmfmac43430-sdio.txt      fc3949a4c32f07c18308e7e145c7615be314158e7d714a80e04e4791f16495f9
echo "wifi firmware in $OUT"
