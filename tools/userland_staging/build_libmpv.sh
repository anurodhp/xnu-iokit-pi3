#!/bin/bash
# libmpv from mpv 0.40.0 (third_party/mpv, github.com/mpv-player/mpv tag
# v0.40.0; built without --enable-gpl, so LGPL-2.1+) for Venice.app:
#   /usr/lib/libmpv.2.dylib, headers in libc_build/media/include/mpv
# (client.h, render.h, render_gl.h, stream_cb.h).
#
# What Venice uses: the client API, and the render API with OpenGL (an
# mpv_render_context drawing into Venice's own GLX context: the VC4 V3D on
# the Pi, softpipe elsewhere) or with the software renderer (MPV_RENDER_API_
# TYPE_SW into an MIT-SHM image). So no video outputs or windowing of mpv's
# own (-Dx11/-Dgl-x11/-Degl off; -Dplain-gl on is "GL through the render
# API, with no platform code"), no Lua/JavaScript, no cplayer.
#
# Two things differ from a stock build, neither of them an edit to mpv:
#   * meson.build runs TOOLS/macos-sdk-version.py for any darwin host, which
#     asks xcrun for the macOS SDK and adds "-isysroot <macOS SDK> -L/usr/lib"
#     to every link (meson.build:1508-1529). This is not macOS: during setup
#     xcrun/xcodebuild are shims that report no macOS SDK, so the script
#     returns its "none" answer ("" , 0.0) and nothing is added.
#   * Audio: mpv has no output for this system's audiod. mpv_darwin/
#     ao_audiohal.c is one (an IOProc client of libaudiohal, which it opens
#     with dlopen, so libmpv does not link it); it is compiled
#     here against mpv's headers and takes the audio_out_oss slot of ao.c's
#     driver table, enabled by setting HAVE_OSS_AUDIO in the generated
#     config.h (the oss-audio feature itself is off: no sys/soundcard.h).
#     Same technique as build_mesa.sh's MESA_VC4 relink.
#   * HAVE_POSIX_SHM is turned off in config.h: meson enables it when
#     shm_open exists (meson.build:990), but this system has no shm_unlink,
#     and only vo_kitty (a terminal video output) uses either.
#   * mpv_darwin/mach_wait_until.c: mpv's darwin timer sleeps with
#     mach_wait_until(), which this libsystem_kernel does not export; libmpv
#     carries a private one on nanosleep().
# meson builds libmpv as a static archive; the dylib is linked here with
# -force_load, the audio output and the owner dylibs, exporting only mpv_*.
#
# Decoding is FFmpeg in software (NEON): the Pi's hardware decoder is not
# reachable from this kernel (build_ffmpeg.sh). The GPU does the YUV->RGB
# conversion, scaling and presentation through libplacebo's GL backend.
set -euo pipefail
cd "$(dirname "$0")"
. ./media_common.sh
# media_common.sh links against these; for gen_build_order.py, which reads only build_*.sh:
# run build_libsystem.sh build_libinfo.sh build_libresolv_dylib.sh build_libm_dylib.sh build_libcxx_dylib.sh build_zlib.sh build_freetype.sh build_fontconfig.sh first
SRC="$PWD/$TP/mpv"
x11_require_source "$SRC/meson.build"
grep -qx "0.40.0" "$SRC/MPV_VERSION" || { echo "error: mpv is not 0.40.0 -- recheck this script" >&2; exit 1; }
for p in libavcodec libavformat libavfilter libswscale libswresample libavutil; do
    [ -f "$MEDIA_PC/$p.pc" ] || { echo "error: $p not built -- run build_ffmpeg.sh first" >&2; exit 1; }
done
[ -f "$MEDIA_PC/libass.pc" ] || { echo "error: libass not built -- run build_libass.sh first" >&2; exit 1; }
[ -f "$MEDIA_PC/libplacebo.pc" ] || { echo "error: libplacebo not built -- run build_libplacebo.sh first" >&2; exit 1; }
# The CoreAudio HAL header (the public AudioHardware.h) comes from the Xcode 12
# macOS SDK, linked into the build directory as coreaudio/build_common.sh does.
MACSDK_CA="$XCODE12/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/System/Library/Frameworks/CoreAudio.framework/Headers"
[ -f "$MACSDK_CA/AudioHardware.h" ] || { echo "error: $MACSDK_CA/AudioHardware.h not found" >&2; exit 1; }
CA_HDRS="$MEDIA_GEN/coreaudio_headers"
mkdir -p "$CA_HDRS"
ln -sfn "$MACSDK_CA" "$CA_HDRS/CoreAudio"

SHIMS="$MEDIA_GEN/mpv-shims"
rm -rf "$SHIMS"; mkdir -p "$SHIMS"
printf '#!/bin/sh\n# build_libmpv.sh: no macOS SDK for this target\ncase "$*" in *--show-sdk-version*) echo 0.0 ;; *) echo ;; esac\n' > "$SHIMS/xcrun"
printf '#!/bin/sh\nexit 1\n' > "$SHIMS/xcodebuild"
chmod +x "$SHIMS/xcrun" "$SHIMS/xcodebuild"

PATH="$SHIMS:$PATH" media_meson mpv "$SRC" --default-library=static \
    -Dlibmpv=true -Dcplayer=false -Dgpl=false -Dtests=false -Dfuzzers=false -Dbuild-date=false \
    -Dgl=enabled -Dplain-gl=enabled -Dx11=disabled -Dgl-x11=disabled -Degl=disabled -Degl-x11=disabled \
    -Dvulkan=disabled -Dwayland=disabled -Ddrm=disabled -Dgbm=disabled -Dvaapi=disabled -Dvdpau=disabled \
    -Dxv=disabled -Dcaca=disabled -Dsixel=disabled -Dsdl2=disabled -Dsdl2-audio=disabled -Dsdl2-video=disabled \
    -Dsdl2-gamepad=disabled -Dcocoa=disabled -Dgl-cocoa=disabled -Dswift-build=disabled -Dmacos-cocoa-cb=disabled \
    -Dmacos-media-player=disabled -Dmacos-touchbar=disabled -Dvideotoolbox-gl=disabled -Dvideotoolbox-pl=disabled \
    -Dios-gl=disabled -Dcoreaudio=disabled -Daudiounit=disabled -Davfoundation=disabled -Doss-audio=disabled \
    -Dalsa=disabled -Dpulse=disabled -Dpipewire=disabled -Djack=disabled -Dopenal=disabled -Dsndio=disabled \
    -Dlua=disabled -Djavascript=disabled -Dcplugins=disabled -Dlibarchive=disabled -Dlibbluray=disabled \
    -Ddvdnav=disabled -Dcdda=disabled -Ddvbin=disabled -Drubberband=disabled -Dvapoursynth=disabled \
    -Dzimg=disabled -Dlcms2=disabled -Djpeg=disabled -Duchardet=disabled -Diconv=disabled -Dshaderc=disabled \
    -Dspirv-cross=disabled -Dlibavdevice=disabled -Dzlib=enabled -Dmanpage-build=disabled \
    -Dhtml-build=disabled -Dpdf-build=disabled
BUILD="$MEDIA_GEN/mpv"
grep -q '^#define HAVE_OSS_AUDIO 0$' "$BUILD/config.h" || { echo "error: config.h has no HAVE_OSS_AUDIO 0 -- recheck this script" >&2; exit 1; }
grep -q '^#define HAVE_GL 1$' "$BUILD/config.h" && grep -q 'libmpv_gl.c' "$BUILD/build.ninja" ||
    { echo "error: mpv configured without the GL render API (plain-gl)" >&2; exit 1; }
# meson regenerates config.h only on a reconfigure, and media_meson always
# configures afresh, so the edit and the objects compiled from it agree:
# rebuild ao.c after setting it.
grep -q '^#define HAVE_POSIX_SHM 1$' "$BUILD/config.h" || { echo "error: config.h has no HAVE_POSIX_SHM 1 -- recheck this script" >&2; exit 1; }
sed -i '' -e 's/^#define HAVE_OSS_AUDIO 0$/#define HAVE_OSS_AUDIO 1/' -e 's/^#define HAVE_POSIX_SHM 1$/#define HAVE_POSIX_SHM 0/' "$BUILD/config.h"
ninja -C "$BUILD" -t clean > /dev/null
ninja -C "$BUILD" libmpv.a > "$MEDIA_GEN/mpv-ninja2.log" 2>&1 ||
    { grep -E 'error:|FAILED' "$MEDIA_GEN/mpv-ninja2.log" | head -20 >&2; exit 1; }

# the audio output and mach_wait_until, with the flags meson used for ao.c
for f in ao_audiohal mach_wait_until; do
python3 -I - "$BUILD" "$PWD/mpv_darwin/$f.c" "$BUILD/$f.o" "$CA_HDRS" <<'PY'
import json, shlex, subprocess, sys
build, src, obj, cah = sys.argv[1:5]
cmds = json.load(open(build + "/compile_commands.json"))
c = next(x for x in cmds if x["file"].endswith("audio/out/ao.c"))
a = shlex.split(c["command"])
out, skip = [], 0
for i, x in enumerate(a):
    if skip: skip -= 1; continue
    if x in ("-o", "-MF", "-MQ"): skip = 1; continue
    if x in ("-MD", "-c") or x.endswith("audio/out/ao.c"): continue
    out.append(x)
out += ["-I" + cah, "-Werror=implicit-function-declaration", "-c", src, "-o", obj]
subprocess.check_call(out, cwd=build)
PY
done

DYLIBS=("$MEDIA_SYS/libass.dylib" "$MEDIA_SYS/libplacebo.dylib" "$MEDIA_SYS/libavfilter.dylib"
    "$MEDIA_SYS/libavformat.dylib" "$MEDIA_SYS/libavcodec.dylib" "$MEDIA_SYS/libswscale.dylib"
    "$MEDIA_SYS/libswresample.dylib" "$MEDIA_SYS/libavutil.dylib" "$MEDIA_SYS/libz.dylib")
LIB="$MEDIA_SYS/libmpv.dylib"
rm -f "$LIB"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib "${MEDIA_LDFLAGS[@]}" \
    -install_name /usr/lib/libmpv.2.dylib -compatibility_version 2 -current_version 2.5 \
    -Wl,-exported_symbol,'_mpv_*' \
    -o "$LIB" -Wl,-force_load,"$BUILD/libmpv.a" "$BUILD/ao_audiohal.o" "$BUILD/mach_wait_until.o" "${DYLIBS[@]}" "${MEDIA_SYSLIBS[@]}" 2> "$BUILD/link.err" ||
    { grep -E '^  "|error' "$BUILD/link.err" | sed 's/, referenced from:.*//' | sort -u >&2; echo "error: libmpv link failed" >&2; exit 1; }
otool -hv "$LIB" | grep -q " DYLIB " || { echo "error: $LIB is not MH_DYLIB" >&2; exit 1; }
# nm's output is read once: under pipefail, "nm | grep -q" fails when grep stops reading early
SYMS="$(nm "$LIB")"
for s in _mpv_create _mpv_initialize _mpv_command_async _mpv_observe_property _mpv_render_context_create \
         _mpv_render_context_render _mpv_set_wakeup_callback; do
    grep -q " T $s$" <<< "$SYMS" || { echo "error: $LIB does not export $s" >&2; exit 1; }
done
grep -q " _audio_out_oss$" <<< "$SYMS" || { echo "error: the audiohal output is not linked in" >&2; exit 1; }
echo "wrote $LIB (/usr/lib/libmpv.2.dylib)"
./bind_target_audit.sh "$LIB"
rm -rf "$MEDIA_INC/mpv"; mkdir -p "$MEDIA_INC/mpv"
cp "$SRC"/include/mpv/*.h "$MEDIA_INC/mpv/"
media_pc mpv 2.5.0 "-I$MEDIA_INC" "$LIB"
