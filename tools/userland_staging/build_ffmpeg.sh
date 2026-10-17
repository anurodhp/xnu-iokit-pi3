#!/bin/bash
# FFmpeg 7.1.1 (third_party/ffmpeg, github.com/FFmpeg/FFmpeg tag n7.1.1) as
# the decoders, demuxers and network protocols under libmpv
# (build_libmpv.sh), for Venice.app:
#   /usr/lib/libavutil.59.dylib  libswresample.5  libswscale.8  libavcodec.61
#   /usr/lib/libavformat.61.dylib  libavfilter.10
# Headers in libc_build/media/include/lib{av*,sw*}.
#
# FFmpeg's own configure + make, cross for darwin/aarch64 with this
# project's -nostdlib link (media_common.sh): the owner dylibs go in
# --extra-libs, so configure's link checks and the final links see exactly
# what the target has. Unmodified upstream.
#
# What is on and off, and why:
#   * NEON (the aarch64 assembly in lib*/aarch64) is on: the Pi 3's
#     Cortex-A53 decodes in software, and that assembly is most of the speed.
#   * The Pi's hardware decoders through MMAL (--enable-mmal: h264_mmal,
#     mpeg2_mmal, mpeg4_mmal, vc1_mmal): the decoder is on the VideoCore VPU,
#     reached through the BCM2837VCHIQ kext and the Raspberry Pi userland
#     libraries (build_rpi_userland.sh). They are hardware decoders in FFmpeg's
#     sense (AV_CODEC_CAP_HARDWARE), so nothing uses them unless asked: mpv's
#     hwdec=mmal-copy does, and gets software frames copied out of the VPU.
#     Where there is no VPU (QEMU) they fail to open and mpv decodes in
#     software. VideoToolbox, AudioToolbox, SecureTransport, CoreImage,
#     AVFoundation, AppKit and Metal (configure enables them for
#     target-os=darwin) are not on this system.
#   * https:// via mbedTLS (build_mbedtls.sh). Its Apache-2.0 licence makes
#     the build LGPL-3 (--enable-version3). No GPL parts.
#   * No programs (ffmpeg/ffprobe), no avdevice, no postproc, no docs.
set -euo pipefail
cd "$(dirname "$0")"
. ./media_common.sh
# media_common.sh links against these; for gen_build_order.py, which reads only build_*.sh:
# run build_libsystem.sh build_libinfo.sh build_libresolv_dylib.sh build_libm_dylib.sh build_libcxx_dylib.sh build_zlib.sh build_freetype.sh build_fontconfig.sh first
SRC="$PWD/$TP/ffmpeg"
x11_require_source "$SRC/configure"
grep -qx "7.1.1" "$SRC/RELEASE" || { echo "error: ffmpeg is not 7.1.1 -- recheck this script" >&2; exit 1; }
[ -f "$MEDIA_PC/mbedtls.pc" ] || { echo "error: mbedtls not built -- run build_mbedtls.sh first" >&2; exit 1; }
# for gen_build_order.py: run build_rpi_userland.sh first
[ -f "$MEDIA_PC/mmal.pc" ] || { echo "error: MMAL not built -- run build_rpi_userland.sh first" >&2; exit 1; }
# configure's mmal check is check_lib with -lmmal_core -lmmal_util
# -lmmal_vc_client -lbcm_host, not pkg-config: headers and those link names
# come from build_rpi_userland.sh's staged tree and link directory.
MMAL_CFLAGS="$(PKG_CONFIG_LIBDIR="$MEDIA_PC" pkg-config --cflags mmal)"
MMAL_LINKDIR="$MEDIA/mmal-linkdir"
BUILD="$MEDIA_GEN/ffmpeg"
DEST="$MEDIA/root/ffmpeg"
rm -rf "$BUILD" "$DEST"
mkdir -p "$BUILD"
cd "$BUILD"
PKG_CONFIG_LIBDIR="$MEDIA_PC" "$SRC/configure" \
    --enable-cross-compile --target-os=darwin --arch=aarch64 --cpu=cortex-a53 \
    --cc="$CLANG" --cxx="$TC/clang++" --ar="$TC/ar" --ranlib="$TC/ranlib" --nm="$TC/nm" --strip="$TC/strip" \
    --pkg-config="$(command -v pkg-config)" --pkg-config-flags=--static \
    --prefix=/usr --libdir=/usr/lib --shlibdir=/usr/lib --incdir=/usr/include \
    --extra-cflags="-isysroot $SDK -target arm64-apple-ios14.4 ${MEDIA_CFLAGS[*]} $MMAL_CFLAGS" \
    --extra-ldflags="-isysroot $SDK -target arm64-apple-ios14.4 ${MEDIA_LDFLAGS[*]} -L$MMAL_LINKDIR" \
    --extra-libs="$MEDIA_SYS/libz.dylib $MEDIA_SYS/libvcos.dylib ${MEDIA_SYSLIBS[*]}" \
    --enable-shared --disable-static --enable-pic --disable-debug --disable-doc --disable-programs \
    --disable-avdevice --disable-postproc --enable-version3 --enable-mbedtls --enable-zlib --enable-mmal \
    --disable-videotoolbox --disable-audiotoolbox --disable-securetransport --disable-coreimage \
    --disable-avfoundation --disable-appkit --disable-metal --disable-iconv --disable-bzlib --disable-lzma \
    --disable-sdl2 --disable-xlib --disable-libxcb --disable-vulkan --disable-opencl --disable-videotoolbox \
    > configure.log 2>&1 || { tail -30 configure.log; tail -40 ffbuild/config.log >&2; echo "error: ffmpeg configure failed" >&2; exit 1; }
# configure adds -framework CoreFoundation/CoreMedia/CoreVideo/CoreAudio for
# target-os=darwin whenever the SDK has their headers (check_apple_framework,
# configure:6685-6688), whether or not anything uses them; none exists here.
sed -i '' -E 's/ ?-framework [A-Za-z]+//g' ffbuild/config.mak
grep -q -- "-framework" ffbuild/config.mak && { echo "error: -framework left in config.mak" >&2; exit 1; }
grep -q "^HAVE_NEON=yes" ffbuild/config.mak || { echo "error: ffmpeg configured without NEON" >&2; exit 1; }
grep -q "^CONFIG_MBEDTLS=yes" ffbuild/config.mak || { echo "error: ffmpeg configured without mbedTLS" >&2; exit 1; }
grep -q "^CONFIG_H264_MMAL_DECODER=yes" ffbuild/config.mak || { echo "error: ffmpeg configured without h264_mmal" >&2; exit 1; }
make -j"$(sysctl -n hw.ncpu)" > make.log 2>&1 ||
    { grep -E 'error:|Undefined|^ld: |^  "' make.log | head -40 >&2; echo "error: ffmpeg build failed, see $BUILD/make.log" >&2; exit 1; }
if grep -qE 'implicit declaration|implicitly declaring' make.log; then
    grep -E 'implicit declaration|implicitly declaring' make.log | sort -u >&2
    echo "error: implicit function declarations in the ffmpeg build (above)" >&2; exit 1
fi
make install DESTDIR="$DEST" > install.log 2>&1 || { tail -20 install.log >&2; exit 1; }
cd - > /dev/null
for l in avutil:59 swresample:5 swscale:8 avcodec:61 avformat:61 avfilter:10; do
    n="${l%%:*}"; m="${l##*:}"
    media_install_dylib "$DEST/usr/lib/lib$n.$m.dylib" "lib$n.dylib" "/usr/lib/lib$n.$m.dylib"
done
for n in avutil swresample swscale avcodec avformat avfilter; do
    rm -rf "$MEDIA_INC/lib$n"
done
cp -R "$DEST/usr/include/." "$MEDIA_INC/"
# .pc files: upstream's, with Libs pointed at the staged dylibs (mpv checks the versions)
for pc in "$DEST"/usr/lib/pkgconfig/*.pc; do
    n="$(basename "$pc" .pc)"
    v="$(sed -n 's/^Version: //p' "$pc")"
    req="$(sed -n 's/^Requires: //p' "$pc" | sed 's/ *= *[0-9.]*//g')"
    media_pc "$n" "$v" "-I$MEDIA_INC" "$MEDIA_SYS/$n.dylib" "$req"
done
