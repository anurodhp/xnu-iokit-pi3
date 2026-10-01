#!/bin/bash
# DAR-460: stage the QEMU framebuffer test onto a mounted SD image's ROOT
# volume: fb_cache_guest_test.sh as /usr/local/bin/fbt.sh and the four
# puredarwingop xorg.conf variants it runs as /etc/X11/xorg-<name>.conf
# (made from x11_config/xorg.conf; the image ships no sed).
# Usage: fb_cache_qemu_prep.sh "/Volumes/ROOT N"
set -euo pipefail
cd "$(dirname "$0")"
R="${1:?usage: $0 <mounted ROOT volume>}"
[ -d "$R/etc/X11" ] && [ -d "$R/usr/local/bin" ] || { echo "error: $R is not a provisioned ROOT volume" >&2; exit 1; }
variant() {  # name mode shadow delay
    sed -e "s/\"VRAMCacheMode\" *\"[a-z]*\"/\"VRAMCacheMode\" \"$2\"/" \
        -e "s/\"ShadowFB\" *\"[a-z]*\"/\"ShadowFB\" \"$3\"/" \
        -e "s/\"ShadowUpdateDelay\" *\"[0-9]*\"/\"ShadowUpdateDelay\" \"$4\"/" \
        -e 's/#Option *"ShadowStats"/Option "ShadowStats"/' \
        x11_config/xorg.conf > "$R/etc/X11/xorg-$1.conf"
    grep -q "\"VRAMCacheMode\" \"$2\"" "$R/etc/X11/xorg-$1.conf" || { echo "error: variant $1 not applied" >&2; exit 1; }
}
variant wc     writecombine on  0
variant dflt   default      on  0
variant direct writecombine off 0
variant delay  writecombine on  16
cp fb_cache_guest_test.sh "$R/usr/local/bin/fbt.sh"
chmod 755 "$R/usr/local/bin/fbt.sh"
echo "staged fbt.sh and xorg-{wc,dflt,direct,delay}.conf on $R"
