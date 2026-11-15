#!/bin/bash
# Downloads the WiFi firmware for both Pi 3 boards (docs/wifi-plan.md):
# BCM43430A1 / CYW43438 (Pi 3 Model B) and CYW43455 (Pi 3 Model B+) into libc_build/wifi_firmware/, at a pinned commit of
# RPi-Distro/firmware-nonfree (what Raspberry Pi OS ships), and checks
# SHA-256s. The files are Cypress binaries under the Cypress
# redistribution licence ("binary-redist-Cypress" for
# debian/added-firmware/*/*43430* and */*43455* in the repo's debian/copyright, fetched
# here too at the same commit), which allows distributing them for use with
# Cypress chips, so the image ships them with that file beside them. Neither
# is committed here (libc_build/ is gitignored). inject_into_sd_image.sh
# runs this script and installs them to /usr/share/firmware/wifi/ under the
# names /usr/sbin/wifi expects.
#
# Raspberry Pi OS resolves brcmfmac43430-sdio.raspberrypi,3-model-b.* to
# exactly these three files (symlinks in debian/added-firmware/brcm/).
# brcmfmac43455-sdio.raspberrypi,3-model-b-plus.* resolve to
# cypress/cyfmac43455-sdio.{bin,clm_blob} and brcm/brcmfmac43455-sdio.txt;
# cyfmac43455-sdio.bin itself is an update-alternatives link whose
# higher-priority (50, vs 10 for -minimal) and so default target is
# cyfmac43455-sdio-standard.bin (debian/config/defines.toml postinst), so
# that is the one fetched. /usr/sbin/wifi picks the set by the chip id the
# kernel probed, so every image carries both and boots on either board.
set -euo pipefail
cd "$(dirname "$0")"

COMMIT=3bab0f823f5b53150b76aab77093adef6655b920
REPO="https://raw.githubusercontent.com/RPi-Distro/firmware-nonfree/$COMMIT/debian"
BASE="$REPO/added-firmware"
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
fetch "$BASE/cypress/cyfmac43455-sdio-standard.bin" brcmfmac43455-sdio.bin  d608f866582519c0a28d86db43040f4f1b98dd1d153e72e9752586546b4a36c3
fetch "$BASE/cypress/cyfmac43455-sdio.clm_blob" brcmfmac43455-sdio.clm_blob 9823842cae9fb9a5dd1e5fb31f595516ec7deee341354bef30bb3026eee29cc1
fetch "$BASE/brcm/brcmfmac43455-sdio.txt"       brcmfmac43455-sdio.txt      ca709be81a78bdb6932936374f39943acbd7af07fae6151011127599a3ce9e3d
fetch "$REPO/copyright"                         copyright                   07082fd0bb65e32c73af39a2292dd6c83f169be7427d338ba1fe34f9b86c0e30
echo "wifi firmware in $OUT"
