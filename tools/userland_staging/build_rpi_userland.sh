#!/bin/bash
# The Raspberry Pi userland libraries FFmpeg's h264_mmal decoder needs
# (third_party/userland: our fork anurodhp/userland, branch iokit-port, of
# github.com/raspberrypi/userland at a54a0db; BSD-3-Clause), for hardware
# H.264 decoding in Venice:
#   /usr/lib/libvcos.dylib        VideoCore OS abstraction (pthreads)
#   /usr/lib/libvchiq_arm.dylib   VCHIQ client, over the BCM2837VCHIQ kext
#   /usr/lib/libbcm_host.dylib    bcm_host_init() + the vchostif services
#                                 (gencmd, dispmanx, tvservice, cec)
#   /usr/lib/libmmal.dylib        MMAL: mmal_core + mmal_util + mmal_vc_client
# Headers in libc_build/media/include/rpi-userland, mmal.pc for FFmpeg.
#
# Upstream's own source lists (interface/vcos/pthreads, interface/vchiq_arm,
# interface/vmcs_host, host_applications/linux/libs/bcm_host and
# interface/mmal/{core,util,vc} CMakeLists.txt), compiled with its global
# definitions (top-level CMakeLists.txt). What the fork changes, all for
# Darwin or for a 64-bit host:
#   - vcos: unnamed semaphores (Darwin has none), thread names, .dylib;
#   - vchiq_arm: the device is the BCM2837VCHIQ IOKit service, not
#     /dev/vchiq (vchiq_darwin.c, ABI in iokit/BCM2837VCHIQ/BCM2837VCHIQUAPI.h);
#   - mmal vc client: 32-bit wire structs and handles for host pointers
#     (mmal_vc_msgs.h "64-bit hosts", mmal_vc_handle.c). Upstream builds no
#     MMAL for arm64 at all for want of exactly this.
#
# One libmmal.dylib, not upstream's three: mmal_core calls into
# mmal_vc_client (mmal_register_component_videocore, its "horrid hack"
# component registration) and mmal_vc_client into mmal_core and mmal_util,
# which ELF shared objects resolve at load time and a two-level-namespace
# Mach-O link cannot. Upstream ships the same umbrella as libmmal.so.
# FFmpeg's configure asks for -lmmal_core -lmmal_util -lmmal_vc_client:
# mmal.pc names libmmal.dylib for that.
#
# No vcsm (VideoCore shared memory): ENABLE_MMAL_VCSM is off, so MMAL's
# zero-copy mode is unavailable and frames come back by VCHIQ bulk copy,
# which is what FFmpeg's h264_mmal does unless asked for MMAL_ENCODING_OPAQUE.
set -euo pipefail
cd "$(dirname "$0")"
. ./media_common.sh
# for gen_build_order.py, which reads only build_*.sh:
# run build_libsystem.sh build_libiokituser_dylib.sh build_corefoundation_dylib.sh first
SRC="$PWD/$TP/userland"
x11_require_source "$SRC/interface/mmal/vc/mmal_vc_handle.c"
for d in libiokituser libCoreFoundation; do
    [ -f "$MEDIA_SYS/$d.dylib" ] || { echo "error: $MEDIA_SYS/$d.dylib not found -- build it first" >&2; exit 1; }
done
[ -d "$OUT/iokitlib_headers" ] || { echo "error: $OUT/iokitlib_headers not found -- run build_libiokituser_dylib.sh first" >&2; exit 1; }

OBJ="$MEDIA_GEN/rpi-userland"
rm -rf "$OBJ"
mkdir -p "$OBJ"

# Top-level CMakeLists.txt: definitions and include directories.
DEFS=(-D_REENTRANT -DUSE_VCHIQ_ARM -DVCHI_BULK_ALIGN=1 -DVCHI_BULK_GRANULARITY=1
    -DOMX_SKIP64BIT -DEGL_SERVER_DISPMANX -D__VIDEOCORE4__ -DTV_SUPPORTED_MODE_NO_DEPRECATED
    -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64)
INCS=(-I "$SRC/host_applications/framework" -I "$SRC" -I "$SRC/interface/vcos/pthreads"
    -I "$SRC/interface/vmcs_host/linux" -I "$SRC/interface/vmcs_host"
    -I "$SRC/interface/vmcs_host/khronos" -I "$SRC/interface/khronos/include"
    -I "$SRC/interface/vchiq_arm" -I "$SRC/host_support/include" -I "$SRC/interface/mmal"
    -I "$SRC/host_applications/linux/libs/bcm_host/include")
IOKIT_INCS=(-DPRIVATE=1 -I "$OUT/iokitlib_headers" -I "$OUT/compat_include" -I "$OUT/cf_headers"
    -I "$TP/xnu-7195/iokit" -I "$TP/xnu-7195/libkern" -idirafter "$TP/xnu-7195/osfmk")

# ucompile <lib> <file relative to $SRC> [extra flags...]
ucompile() {
    local lib="$1" f="$2" o
    shift 2
    mkdir -p "$OBJ/$lib"
    o="$OBJ/$lib/$(echo "${f%.c}" | tr '/' '_').o"
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c "${MEDIA_CFLAGS[@]}" -Wall \
        -Werror=implicit-function-declaration -Werror=int-conversion -Werror=incompatible-pointer-types \
        -Wno-unused-function -Wno-unused-variable -Wno-multichar \
        "${DEFS[@]}" "${INCS[@]}" "$@" "$SRC/$f" -o "$o" 2> "$o.err" ||
        { cat "$o.err" >&2; echo "error: $f failed to compile" >&2; exit 1; }
    if grep -q "warning:" "$o.err"; then
        grep -h "warning:" "$o.err" | sed "s|^|  [$lib] |" >&2
    fi
}

VCOS="interface/vcos/pthreads/vcos_pthreads.c interface/vcos/pthreads/vcos_dlfcn.c
    interface/vcos/pthreads/vcos_darwin_sem.c interface/vcos/glibc/vcos_backtrace.c
    interface/vcos/generic/vcos_generic_event_flags.c interface/vcos/generic/vcos_mem_from_malloc.c
    interface/vcos/generic/vcos_generic_named_sem.c interface/vcos/generic/vcos_generic_safe_string.c
    interface/vcos/generic/vcos_generic_reentrant_mtx.c interface/vcos/generic/vcos_abort.c
    interface/vcos/generic/vcos_cmd.c interface/vcos/generic/vcos_init.c
    interface/vcos/generic/vcos_msgqueue.c interface/vcos/generic/vcos_logcat.c
    interface/vcos/generic/vcos_generic_blockpool.c"
VCHIQ="interface/vchiq_arm/vchiq_lib.c interface/vchiq_arm/vchiq_util.c interface/vchiq_arm/vchiq_darwin.c"
VCHOSTIF="interface/vmcs_host/linux/vcmisc.c interface/vmcs_host/vc_vchi_gencmd.c
    interface/vmcs_host/vc_vchi_gpuserv.c interface/vmcs_host/vc_vchi_tvservice.c
    interface/vmcs_host/vc_vchi_cecservice.c interface/vmcs_host/vc_vchi_dispmanx.c
    interface/vmcs_host/vc_service_common.c host_applications/linux/libs/bcm_host/bcm_host.c"
MMAL="interface/mmal/core/mmal_format.c interface/mmal/core/mmal_port.c
    interface/mmal/core/mmal_port_clock.c interface/mmal/core/mmal_component.c
    interface/mmal/core/mmal_buffer.c interface/mmal/core/mmal_queue.c interface/mmal/core/mmal_pool.c
    interface/mmal/core/mmal_events.c interface/mmal/core/mmal_logging.c interface/mmal/core/mmal_clock.c
    interface/mmal/util/mmal_il.c interface/mmal/util/mmal_util.c interface/mmal/util/mmal_connection.c
    interface/mmal/util/mmal_graph.c interface/mmal/util/mmal_list.c interface/mmal/util/mmal_param_convert.c
    interface/mmal/util/mmal_util_params.c interface/mmal/util/mmal_component_wrapper.c
    interface/mmal/util/mmal_util_rational.c
    interface/mmal/vc/mmal_vc_client.c interface/mmal/vc/mmal_vc_shm.c interface/mmal/vc/mmal_vc_api.c
    interface/mmal/vc/mmal_vc_opaque_alloc.c interface/mmal/vc/mmal_vc_msgnames.c
    interface/mmal/vc/mmal_vc_api_drm.c interface/mmal/vc/mmal_vc_handle.c"

# The MMAL messages' layouts on this 64-bit host must be exactly what a
# 32-bit ARM Linux host sends (the firmware's MMAL worker's ABI): dump both
# with clang and compare (mmal_abi_check/).
ABI="$OBJ/abi"
mkdir -p "$ABI"
ABI_INCS=(-D_REENTRANT -DUSE_VCHIQ_ARM -I "$SRC" -I "$SRC/interface/vcos/pthreads"
    -I "$SRC/interface/vmcs_host/linux" -I "$SRC/interface/vchiq_arm" -I "$SRC/interface/mmal")
"$CLANG" -target armv7-unknown-linux-gnueabihf -idirafter "$SDK/usr/include" -D__APPLE__ -fsyntax-only \
    -Xclang -fdump-record-layouts-simple "${ABI_INCS[@]}" mmal_abi_check/wire_types.c > "$ABI/arm32.txt"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -fsyntax-only \
    -Xclang -fdump-record-layouts-simple "${ABI_INCS[@]}" mmal_abi_check/wire_types.c > "$ABI/arm64.txt"
python3 mmal_abi_check/mmal_abi_check.py "$ABI/arm32.txt" "$ABI/arm64.txt"

for f in $VCOS; do ucompile vcos "$f" -I "$SRC/interface/vcos" -I "$SRC/interface/vcos/generic"; done
for f in $VCHIQ; do ucompile vchiq_arm "$f" "${IOKIT_INCS[@]}"; done
for f in $VCHOSTIF; do ucompile bcm_host "$f" -fno-strict-aliasing; done
for f in $MMAL; do ucompile mmal "$f" -I "$SRC/host_applications/linux/libs/sm"; done

# ulink <dylib> <compat version> <objdir> [dylibs it links...]
ulink() {
    local name="$1" ver="$2" objdir="$3" out="$MEDIA_SYS/$1.dylib"
    shift 3
    rm -f "$out"
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib "${MEDIA_LDFLAGS[@]}" \
        -install_name "/usr/lib/$name.dylib" -compatibility_version "$ver" -current_version "$ver" \
        -o "$out" "$OBJ/$objdir"/*.o "$@" "${MEDIA_SYSLIBS[@]}"
    otool -hv "$out" | grep -q " DYLIB " || { echo "error: $out is not MH_DYLIB" >&2; exit 1; }
    echo "wrote $out (/usr/lib/$name.dylib)"
    ./bind_target_audit.sh "$out"
}
ulink libvcos 1.0.0 vcos
ulink libvchiq_arm 1.0.0 vchiq_arm "$MEDIA_SYS/libvcos.dylib" \
    "$MEDIA_SYS/libiokituser.dylib" "$MEDIA_SYS/libCoreFoundation.dylib"
ulink libbcm_host 1.0.0 bcm_host "$MEDIA_SYS/libvchiq_arm.dylib" "$MEDIA_SYS/libvcos.dylib"
ulink libmmal 1.0.0 mmal "$MEDIA_SYS/libvchiq_arm.dylib" "$MEDIA_SYS/libvcos.dylib"

for sym in _vchiq_initialise _vchiq_darwin_ioctl; do
    nm -gU "$MEDIA_SYS/libvchiq_arm.dylib" | grep -q " $sym$" || { echo "error: libvchiq_arm.dylib does not export $sym" >&2; exit 1; }
done
for sym in _mmal_component_create _mmal_vc_init _mmal_port_connect _mmal_util_rgb_order_fixed _mmal_vc_handle_register; do
    nm -gU "$MEDIA_SYS/libmmal.dylib" | grep -q " $sym$" || { echo "error: libmmal.dylib does not export $sym" >&2; exit 1; }
done

# Headers: what upstream installs under /opt/vc/include (the interface/
# tree, vcos's platform headers, bcm_host.h), here as one staged tree.
H="$MEDIA_INC/rpi-userland"
rm -rf "$H"
mkdir -p "$H"
(cd "$SRC" && find interface vcinclude -name '*.h' -print0 | rsync -a --from0 --files-from=- ./ "$H/")
cp "$SRC/host_applications/linux/libs/bcm_host/include/bcm_host.h" "$H/"
media_pc mmal 1.0.0 "-I$H -I$H/interface/vcos/pthreads -I$H/interface/vmcs_host/linux" \
    "$MEDIA_SYS/libmmal.dylib $MEDIA_SYS/libbcm_host.dylib $MEDIA_SYS/libvchiq_arm.dylib $MEDIA_SYS/libvcos.dylib"
# The names FFmpeg's configure check (configure: "check_lib mmal ... -lmmal_core
# -lmmal_util -lmmal_vc_client -lbcm_host") links against, for the link only
# (build_ffmpeg.sh adds this directory with -L).
MMAL_LINKDIR="$MEDIA/mmal-linkdir"
rm -rf "$MMAL_LINKDIR"
mkdir -p "$MMAL_LINKDIR"
for n in libmmal_core libmmal_util libmmal_vc_client; do
    ln -s "$MEDIA_SYS/libmmal.dylib" "$MMAL_LINKDIR/$n.dylib"
done
ln -s "$MEDIA_SYS/libbcm_host.dylib" "$MMAL_LINKDIR/libbcm_host.dylib"
echo "rpi userland: libvcos, libvchiq_arm, libbcm_host, libmmal built"
