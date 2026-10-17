#!/bin/bash
# The Pi's hardware video decoding, tested without Venice. Builds:
#   libc_build/hwdec_compare.macho        (/usr/local/bin/hwdec_compare)
#       hwdec_compare.c: decodes a file with FFmpeg's h264 and h264_mmal
#       and checks the VPU's frames are bit-identical (see its header)
#   libc_build/mmal_example_basic_1.macho (/usr/local/bin/mmal_example_basic_1)
#       upstream's interface/mmal/test/examples/example_basic_1.c,
#       unmodified: MMAL alone, no FFmpeg (bcm_host_init, a
#       vc.ril.video_decode component, 500 iterations of its loop)
#   libc_build/vcgencmd.macho             (/usr/local/bin/vcgencmd)
#       upstream's gencmd.c: firmware commands over VCHIQ (clocks, memory)
#   libc_build/hwdec/hwdec_test_720p.h264 and .mp4
#       (/usr/local/share/hwdec/): 300 frames of FFmpeg's testsrc2 at
#       1280x720/30, H.264 High (libx264, CRF 23), encoded by the host's
#       ffmpeg -- the raw Annex B stream for example_basic_1, an MP4 for
#       hwdec_compare and Venice
# Installed on real-hardware images too (inject_into_sd_image.sh), like the
# audio tools: QEMU has no VPU.
#
# On the Pi:
#   hwdec_compare /usr/local/share/hwdec/hwdec_test_720p.mp4
#   mmal_example_basic_1 /usr/local/share/hwdec/hwdec_test_720p.h264
set -euo pipefail
cd "$(dirname "$0")"
. ./media_common.sh
# for gen_build_order.py: run build_rpi_userland.sh build_ffmpeg.sh first
for d in libavformat libavcodec libavutil libmmal libbcm_host libvchiq_arm libvcos; do
    [ -f "$MEDIA_SYS/$d.dylib" ] || { echo "error: $MEDIA_SYS/$d.dylib not found -- build it first" >&2; exit 1; }
done
T="../../third_party"
SRC="$T/userland"
OBJ="$MEDIA_GEN/hwdec_smoketest"
rm -rf "$OBJ"
mkdir -p "$OBJ"
CC=("$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4)

# exe <out.macho> <object> [dylibs...]: a dynamic executable, Csu's start
# (as build_audio_play.sh), owners before libSystem.B, audited.
exe() {
    local m="$OUT/$1" o="$2"
    shift 2
    rm -f "$m"
    "${CC[@]}" -nostdlib "-B$NEWLD_BINDIR" -Wl,-fixup_chains -Wl,-dead_strip_dylibs \
        -o "$m" -DCRT_NO_PTHREAD_BOOTSTRAP "$T/Csu/start.s" "$T/Csu/crt.c" "$o" "$@" "${MEDIA_SYSLIBS[@]}"
    otool -l "$m" | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || { echo "error: $m has no LC_LOAD_DYLINKER" >&2; exit 1; }
    otool -l "$m" | grep -q "cmd LC_MAIN" || { echo "error: $m has no LC_MAIN" >&2; exit 1; }
    ./bind_target_audit.sh "$m"
    echo "wrote $m"
}

"${CC[@]}" -c "${MEDIA_CFLAGS[@]}" -Wall -Werror=implicit-function-declaration \
    hwdec_compare.c -o "$OBJ/hwdec_compare.o"
exe hwdec_compare.macho "$OBJ/hwdec_compare.o" \
    "$MEDIA_SYS/libavformat.dylib" "$MEDIA_SYS/libavcodec.dylib" "$MEDIA_SYS/libavutil.dylib"

UINC=(-I "$MEDIA_INC/rpi-userland" -I "$MEDIA_INC/rpi-userland/interface/vcos/pthreads"
    -I "$MEDIA_INC/rpi-userland/interface/vmcs_host/linux" -I "$MEDIA_INC/rpi-userland/interface/mmal")
"${CC[@]}" -c "${MEDIA_CFLAGS[@]}" -Wall -Werror=implicit-function-declaration -Wno-unused-variable \
    "${UINC[@]}" "$SRC/interface/mmal/test/examples/example_basic_1.c" -o "$OBJ/example_basic_1.o"
exe mmal_example_basic_1.macho "$OBJ/example_basic_1.o" \
    "$MEDIA_SYS/libmmal.dylib" "$MEDIA_SYS/libbcm_host.dylib" "$MEDIA_SYS/libvchiq_arm.dylib" "$MEDIA_SYS/libvcos.dylib"

# vcgencmd: upstream's host_applications/linux/apps/gencmd/gencmd.c,
# unmodified -- the firmware's general command service over VCHIQ
# (`vcgencmd measure_clock v3d`, `vcgencmd get_mem gpu`, ...). Its /dev/vcio
# mailbox fallback (no VCHIQ) does not exist here. It times -t with clock(),
# which this system's libsystem_c does not export: Apple's own
# Libc/gen/FreeBSD/clock.c (getrusage) is linked in for it.
"${CC[@]}" -c "${MEDIA_CFLAGS[@]}" -Wall -Werror=implicit-function-declaration -Wno-unused-variable \
    "${UINC[@]}" "$SRC/host_applications/linux/apps/gencmd/gencmd.c" -o "$OBJ/gencmd.o"
"${CC[@]}" -c "${MEDIA_CFLAGS[@]}" -Werror=implicit-function-declaration "$T/Libc/gen/FreeBSD/clock.c" -o "$OBJ/clock.o"
exe vcgencmd.macho "$OBJ/gencmd.o" "$OBJ/clock.o" \
    "$MEDIA_SYS/libbcm_host.dylib" "$MEDIA_SYS/libvchiq_arm.dylib" "$MEDIA_SYS/libvcos.dylib"

# The test clip. testsrc2 is FFmpeg's built-in generator: deterministic, so
# every build makes the same pictures (the encoder may differ by version,
# which hwdec_compare does not care about: it compares two decodes).
command -v ffmpeg >/dev/null || { echo "error: no host ffmpeg for the test clip (brew install ffmpeg)" >&2; exit 1; }
CLIP="$OUT/hwdec"
mkdir -p "$CLIP"
ffmpeg -nostdin -loglevel error -y -f lavfi -i testsrc2=size=1280x720:rate=30 -frames:v 300 \
    -c:v libx264 -profile:v high -pix_fmt yuv420p -crf 23 -bf 2 "$CLIP/hwdec_test_720p.mp4"
ffmpeg -nostdin -loglevel error -y -i "$CLIP/hwdec_test_720p.mp4" -c:v copy -bsf:v h264_mp4toannexb \
    -f h264 "$CLIP/hwdec_test_720p.h264"
echo "wrote $CLIP/hwdec_test_720p.mp4 and .h264"
