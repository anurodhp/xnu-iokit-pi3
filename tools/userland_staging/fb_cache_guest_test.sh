#!/bin/sh
# DAR-460 guest-side QEMU test (boot with hdmi_console=1). The image has
# no sed, so fb_cache_qemu_prep.sh writes the four xorg.conf variants to
# /etc/X11/xorg-<name>.conf and this script to /usr/local/bin/fbt.sh on a
# mounted image. Run as root: sh /usr/local/bin/fbt.sh [bench-iterations]
#  1. fb_cache_test: every VRAM cache mode maps, writes land, throughput.
#  2. fb_cache_test -T: one band per second through a WC mapping (DAR-443).
#  3. Xorg -retro under four puredarwingop configurations (cache mode x
#     ShadowFB x ShadowUpdateDelay), each with ShadowStats on: prints a
#     SNAP-<name> marker and waits 10 s (take a QEMU monitor screendump
#     then), runs x11_bench against it, prints the driver's log lines.
N=${1:-1}
B=/usr/local/bin/fb_cache_test
echo "== FB1 cache modes"
$B -r 2 default wc inhibit posted
echo "== FB2 bands"
$B -r 1 -T 12 wc
run_x() {
    name=$1; mode=$2; shadow=$3; delay=$4
    rm -f /tmp/.X0-lock /tmp/.X11-unix/X0
    echo "== X-$name mode=$mode shadow=$shadow delay=$delay"
    /usr/X11/bin/Xorg :0 -retro -nolisten tcp -config /etc/X11/xorg-$name.conf \
        -logfile /tmp/Xorg-$name.log > /tmp/Xorg-$name.out 2>&1 &
    xpid=$!
    i=0
    while [ $i -lt 90 ]; do
        grep -q "rendering into" /tmp/Xorg-$name.log 2>/dev/null && break
        sleep 1; i=$((i+1))
    done
    sleep 8
    echo "SNAP-$name"
    sleep 10
    /usr/X11/bin/x11_bench :0 $N 2>&1 | grep -E "BENCH (X|x)|FAIL"
    sleep 11
    kill $xpid; sleep 4
    grep -E "puredarwingop|PDGOP|VRAM|Shadow|rendering|ShadowStats" /tmp/Xorg-$name.log | cut -c1-200
    grep -E "PDGOP" /tmp/Xorg-$name.out | head -3
}
run_x wc wc on 0
run_x dflt default on 0
run_x direct wc off 0
run_x delay wc on 16
echo "== FBEND"
