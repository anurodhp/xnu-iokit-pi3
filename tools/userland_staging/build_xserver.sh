#!/bin/bash
# Builds the X servers Xvfb and Xorg (staged as
# libc_build/x11/server/root/usr/X11/bin/{Xvfb,Xorg}) from real, unmodified
# xorg-server-21.1.24 (third_party/xserver), from one meson configuration.
#   Xvfb (DAR-387, X11GS 4): the server core (dix, os, fb, mi, Xext, render,
#        xkb, ...) with the virtual framebuffer DDX.
#   Xorg (DAR-389, X11GS 7): the same core with the hw/xfree86 DDX, which
#        loads video drivers (build_xf86_video_puredarwingop.sh) as MH_BUNDLE
#        modules linked with -bundle_loader against this Xorg. That is what
#        meson's own recipe does (hw/xfree86/dixmods/meson.build: modules are
#        `link_with: e`, the Xorg executable). fb, ddc, i2c, ramdac and the
#        VidMode extension are linked into Xorg itself (hw/xfree86/
#        meson.build:52-67), and on Darwin the OS layer is xserver's stub
#        os-support (hw/xfree86/os-support/meson.build:141-153).
#
# How it is built. xorg-server only has a meson build, and its config
# headers (dix-config.h, xkb-config.h, version-config.h, ...) come from
# meson's feature probes. So this runs xserver's own meson.build as a cross
# build for this project's toolchain (Xcode 12 clang, iPhoneOS 14.4 SDK,
# arm64-apple-ios14.4) and lets ninja compile every object the servers need. The
# final link is done here instead of by meson: -nostdlib against this
# project's libc_build/system/*.dylib with each owner listed, then
# bind_target_audit.sh, like every other executable (fontstack_common.sh
# fs_link_exe). PureDarwin builds Xvfb the same way
# (third_party/PureDarwin nix/pkgs/x11/xvfb.nix); its traps, and how each is
# handled here:
#   - -isysroot goes in the cross file's `c =` entry, not c_args: meson runs
#     has_header/has_header_symbol probes without c_args, so every probe
#     would fail and SCM_RIGHTS, PTHREAD_MUTEX_RECURSIVE, strndup... would be
#     misdetected.
#   - _XSERVER64: include/meson.build:48-49 sets it only if
#     cc.sizeof('unsigned long') == 8, which returned -1 in PureDarwin's
#     cross build. This script checks the generated dix-config.h and fails
#     if it is missing, rather than editing meson.build.
#   - input_thread=false: with the input thread, ospoll's poll() backend
#     returns at once and InputThreadDoWork busy-loops on Darwin.
#   - _FORTIFY_SOURCE=0 is set, for the reason x11_common.sh gives
#     (build_vim.sh's self-recursing __vsprintf_chk).
# Neither of PureDarwin's two patches is needed:
#   - xorg-xvfb-present-card32.patch: 21.1.24 already declares
#     present_select_input(..., CARD32 eid, ...) (present/present_priv.h:
#     323-326), and XID is CARD32 under _XSERVER64.
#   - xorg-xvfb-local-sha1.patch: os/xsha1.c already supports a libmd-style
#     <sha1.h> (xsha1.c:36-49, HAVE_SHA1_IN_LIBMD), and meson's sha1=libmd
#     probe is cc.find_library('md') (meson.build:342-347). This script
#     builds that libmd from OpenBSD's public-domain sha1.c as carried by
#     the OpenSSH pin (third_party/OpenSSH/openssh/openbsd-compat/sha1.c,
#     the same file build_sshd.sh compiles), with its explicit_bzero.c and a
#     three-line includes.h standing in for OpenSSH's (see below). There is
#     no CommonCrypto CC_SHA1 in this system for the Apple-native choice.
#
# Options (meson_options.txt; tickets DAR-387/389): Xvfb and Xorg
# (-Dxorg=true; int10, vgahw, dga, pciaccess and the inputtest driver off,
# fallback_input_driver empty since no input driver is built yet); no GLX, glamor,
# DRI, udev, hal, systemd-logind, pciaccess, XDMCP or docs. listen_unix and
# listen_local keep their defaults (true), so DISPLAY=:0 works through
# /tmp/.X11-unix/X0. mitshm=true: Xext/shm.c probes the SysV shm syscalls
# at start-up behind a SIGSYS handler, so it disables itself cleanly on a
# kernel without them (DAR-407). Paths: prefix /usr/X11; xkb_dir
# /usr/X11/share/X11/xkb and xkb_bin_dir /usr/X11/bin match
# build_xkeyboard_config.sh and build_xkbcomp.sh; xkb_output_dir keeps its
# default, xkb_dir/compiled (meson.build:127-129), created in staging here;
# log_dir /var/log instead of the prefix default /usr/X11/var/log;
# default_font_path /usr/share/fonts is where build_fonts.sh installs fonts;
# the server appends libXfont2's "built-ins" (the core "fixed"/"cursor"
# fonts it needs to start) itself (dix/dixfonts.c:1714-1729).
set -e
cd "$(dirname "$0")"
. ./fontstack_common.sh

SRC="$TP/xserver"
SSHCOMPAT="$TP/OpenSSH/openssh/openbsd-compat"
x11_require_source "$SRC/meson.build"
x11_require_source "$SSHCOMPAT/sha1.c"
grep -q "version: '21.1.24'" "$SRC/meson.build" || { echo "error: xserver is not 21.1.24 -- recheck this script" >&2; exit 1; }
# Host tools: meson >= 0.47 (xserver's meson.build project() line), ninja,
# pkg-config.
for t in meson ninja pkg-config; do
    command -v "$t" >/dev/null || { echo "error: $t not found on the host" >&2; exit 1; }
done
x11_require_staged X11/Xproto.h build_xorg_headers.sh
x11_require_staged X11/Xtrans/Xtrans.h build_xorg_headers.sh
x11_require_staged X11/fonts/libxfont2.h build_libXfont2.sh
x11_require_staged X11/extensions/XKBfile.h build_libxkbfile.sh
x11_require_staged pixman-1/pixman.h build_pixman.sh
for d in libXfont2 libxkbfile libpixman-1 libXau libfontenc libfreetype libz libsystem_m libxcvt; do
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- build it first" >&2; exit 1; }
done
x11_require_sys_dylibs

GEN="$PWD/$X11_ROOT/gen/xserver"
BUILD="$PWD/$X11_ROOT/gen/xserver-build"
INC="$PWD/$X11_INC"
STAGE="$X11_ROOT/server/root"
rm -rf "$GEN" "$BUILD"
mkdir -p "$GEN/pkgconfig" "$GEN/md/include" "$GEN/md/obj" "$STAGE/usr/X11/bin"

# --- pkg-config files for the staged headers -------------------------
# xorgproto's own *.pc.in (every protocol the server asks for), and
# xtrans's, with prefix/includedir pointed at the staged headers. xtrans's
# two substitutions are what its configure.ac:47-58 produces here: fchown
# is exported by libsystem_kernel.dylib (DAR-387 added it; the stub list in
# build_libsystem_kernel.sh), and the sticky bit is unconditional. pixman,
# xkbfile, xfont2 and xau get minimal Cflags-only files: their dylibs are
# linked by this script, not by meson.
for pc in "$TP"/xorgproto/*.pc.in "$TP"/xtrans/xtrans.pc.in; do
    sed -e "s|@prefix@|$INC|g" -e "s|@includedir@|$INC|g" -e "s|@datarootdir@|$INC|g" \
        -e "s|@datadir@|$INC|g" -e "s|@exec_prefix@|$INC|g" -e "s|@libdir@|$INC|g" \
        -e "s|@PACKAGE_VERSION@|1.6.0|g" \
        -e "s|@fchown_define@|-DHAS_FCHOWN|g" -e "s|@sticky_bit_define@|-DHAS_STICKY_DIR_BIT|g" \
        "$pc" > "$GEN/pkgconfig/$(basename "${pc%.in}")"
done
simple_pc() {
    printf 'Name: %s\nDescription: %s (staged, DAR-387)\nVersion: %s\nCflags: %s\n' \
        "$1" "$1" "$2" "$3" > "$GEN/pkgconfig/$1.pc"
}
simple_pc pixman-1 0.46.4 "-I$INC/pixman-1"
simple_pc xkbfile 1.1.3 "-I$INC"
simple_pc xfont2 2.0.7 "-I$INC"
simple_pc xau 1.0.12 "-I$INC"
simple_pc libxcvt 0.1.3 "-I$INC"

# --- libmd (SHA-1) ----------------------------------------------------
# OpenSSH's openbsd-compat sources include its generated includes.h; the
# only things sha1.c and explicit_bzero.c use from it are <sys/types.h>,
# sha1.h, and OpenBSD's DEF_WEAK() (a symbol-visibility macro, empty
# outside OpenBSD's own libc). HAVE_MEMSET_S stays undefined because no
# dylib here exports memset_s, so explicit_bzero.c uses its volatile
# bzero pointer (explicit_bzero.c:30-50).
cp "$SSHCOMPAT/sha1.h" "$GEN/md/include/sha1.h"
cat > "$GEN/md/includes.h" <<'EOF'
/* Stand-in for OpenSSH's generated includes.h (build_xserver.sh). */
#include <sys/types.h>
#include <string.h>
#include "sha1.h"
#define DEF_WEAK(x)
void explicit_bzero(void *, size_t);
EOF
for f in sha1 explicit_bzero; do
    "$CLANG" "${X11_CFLAGS[@]}" -UHAVE_CONFIG_H -I "$GEN/md" -I "$GEN/md/include" \
        -Wno-attributes -Wno-unknown-attributes \
        "$SSHCOMPAT/$f.c" -o "$GEN/md/obj/$f.o"
done
rm -f "$GEN/md/libmd.a"
"$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/ar" rcs "$GEN/md/libmd.a" "$GEN/md/obj"/*.o

# --- cross file -------------------------------------------------------
# Not libremovefile.dylib: it leaks a non-owner _reallocarray export
# (DAR-410), which the iPhoneOS 14.4 SDK does not declare, so the probe
# would pass and every reallocarray() call would be an implicit
# declaration. Nothing meson probes for lives there.
PROBE_DYLIBS=("${X11_SYS_DYLIBS[@]}" "$OUT/system/libsystem_m.dylib" "$OUT/system/libresolv.dylib"
    "$OUT/system/libinfo.dylib" "$OUT/system/libdispatch.dylib")
TC="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin"
{
    echo "[binaries]"
    echo "c = ['$CLANG', '-isysroot', '$SDK', '-target', 'arm64-apple-ios14.4']"
    echo "ar = '$TC/ar'"
    echo "strip = '$TC/strip'"
    echo "pkg-config = '$(command -v pkg-config)'"
    echo "[built-in options]"
    echo "c_args = ['-fno-builtin', '-fno-stack-protector', '-fno-common', '-D_FORTIFY_SOURCE=0',"
    echo "          '-Wno-nullability-completeness', '-Wno-deprecated-declarations',"
    echo "          '-I$INC', '-I$FS_SYSINC', '-I$GEN/md/include',"
    echo "          '-include', '$PWD/xserver_reallocarray_decl.h']"
    # Probe links go against THIS system's dylibs, not the SDK's
    # libSystem: meson's has_function() checks then report what Xvfb will
    # really find at run time (backtrace, clock_gettime... are in the SDK
    # but not exported here, so their HAVE_* stay unset, as configure on
    # this system would leave them).
    printf "c_link_args = ['-nostdlib', '-B%s', '-L%s'" "$NEWLD_BINDIR" "$GEN/md"
    for d in "${PROBE_DYLIBS[@]}"; do printf ", '%s'" "$PWD/$d"; done
    echo "]"
    echo "[properties]"
    echo "pkg_config_libdir = '$GEN/pkgconfig'"
    echo "[host_machine]"
    echo "system = 'darwin'"
    echo "cpu_family = 'aarch64'"
    echo "cpu = 'arm64'"
    echo "endian = 'little'"
} > "$GEN/cross.ini"

meson setup "$BUILD" "$SRC" --cross-file "$GEN/cross.ini" \
    --prefix=/usr/X11 --buildtype=plain \
    -Dxorg=true -Dxvfb=true -Dxephyr=false -Dxnest=false -Dxwin=false -Dxquartz=false \
    -Dglamor=false -Dglx=false -Ddri1=false -Ddri2=false -Ddri3=false -Ddrm=false \
    -Dudev=false -Dudev_kms=false -Dhal=false -Dsystemd_logind=false -Dpciaccess=false \
    -Dxdmcp=false -Dxdm-auth-1=false -Dsecure-rpc=false -Dxselinux=false -Ddtrace=false \
    -Dlibunwind=false -Dsuid_wrapper=false -Dint10=false -Dvgahw=false \
    -Dlinux_apm=false -Dlinux_acpi=false -Dagp=false -Ddga=false \
    -Dfallback_input_driver= -Dxf86-input-inputtest=false \
    -Dlog_dir=/var/log \
    -Ddocs=false -Ddevel-docs=false -Ddocs-pdf=false \
    -Dinput_thread=false -Dmitshm=true -Dsha1=libmd \
    -Dxkb_dir=/usr/X11/share/X11/xkb -Dxkb_bin_dir=/usr/X11/bin \
    -Ddefault_font_path=/usr/share/fonts \
    > "$GEN/meson-setup.log" 2>&1 || { tail -30 "$GEN/meson-setup.log" >&2; exit 1; }
grep -q '#define _XSERVER64 1' "$BUILD/include/dix-config.h" || {
    echo "error: generated dix-config.h lacks _XSERVER64 (see this script's header)" >&2; exit 1; }

# --- compile and link ---------------------------------------------------
# Each server's link inputs are read from ninja's own link command for its
# target: its objects, its plain archives, and (for Xorg, whose libraries
# are meson link_whole) the -Wl,-force_load <archive> pairs, kept as
# force-loads. ninja then builds exactly those inputs, and the link is done
# here. ninja's -l flags for both are -ldl -lm -lmd: libdyld, libsystem_m
# and the libmd.a above. The network and file calls go to the dylibs that
# own them in this tree (the same owners build_libX11.sh uses):
# inet_ntop/inet_pton and friends in libresolv, getaddrinfo and the passwd
# lookups in libinfo, rename in libremovefile. libdispatch: os/client.c:
# 155-157, xserver's own __APPLE__ path, uses dispatch_once(). Only dylibs
# a server binds are listed: neither binds libxkbfile, libfontenc,
# freetype or zlib (the server runs xkbcomp as a separate program, and
# libXfont2 links the font libraries itself). meson's Xorg link also passes
# -Wl,-export_dynamic; that only keeps symbols alive under LTO, which this
# build does not use, and an ld64 executable exports its globals anyway
# (Xorg's 1888 exports are what the driver binds, checked by
# build_xf86_video_puredarwingop.sh).
SERVER_DYLIBS=("$OUT/system/libXfont2.dylib" "$OUT/system/libpixman-1.dylib" "$OUT/system/libXau.dylib"
    "$OUT/system/libsystem_m.dylib" "$OUT/system/libresolv.dylib" "$OUT/system/libinfo.dylib"
    "$OUT/system/libremovefile.dylib" "$OUT/system/libdispatch.dylib")

# build_server <ninja target> <name> [extra dylibs...]
build_server() {
    local target="$1" name="$2"
    shift 2
    local cmd inputs objs archives forceload
    cmd=$(cd "$BUILD" && ninja -t commands "$target" | tail -1 | tr ' ' '\n')
    objs=$(echo "$cmd" | grep -v '^-' | grep '\.o$' || true)
    # meson writes a link_whole archive as two arguments, "-Wl,-force_load"
    # then the path; every other .a is a plain archive.
    forceload=$(echo "$cmd" | awk 'prev == "-Wl,-force_load" && /\.a$/ { print } { prev = $0 }')
    archives=$(echo "$cmd" | awk 'prev != "-Wl,-force_load" && !/^-/ && /\.a$/ { print } { prev = $0 }')
    [ "$(echo "$objs" | grep -c .)" -ge 1 ] && [ "$(echo "$archives $forceload" | wc -w)" -ge 10 ] || {
        echo "error: could not read $name's link inputs from build.ninja" >&2; exit 1; }
    (cd "$BUILD" && ninja $objs $archives $forceload) > "$GEN/ninja-$name.log" 2>&1 || {
        grep -E 'error:|FAILED' "$GEN/ninja-$name.log" | head -40 >&2; exit 1; }
    # An implicit declaration truncates a pointer return on arm64 (DAR-377's
    # class of bug), so any is fatal.
    local implicit
    implicit=$(grep -E 'implicit declaration|implicitly declaring' "$GEN/ninja-$name.log" | sort -u || true)
    if [ -n "$implicit" ]; then
        echo "$implicit" >&2
        echo "error: implicit function declarations in the $name build (above)" >&2; exit 1
    fi
    local linkdir="$GEN/link-$name"
    rm -rf "$linkdir"
    mkdir -p "$linkdir"
    # Copied under a flat, visible name: mi/miinitext.c's object is
    # <target>.p/.._.._mi_miinitext.c.o, a dotfile that fs_link_exe's *.o
    # glob would skip (InitExtensions and friends would go missing).
    local o
    for o in $objs; do
        cp "$BUILD/$o" "$linkdir/$(basename "$o" | sed 's/^[._]*//')"
    done
    [ "$(ls "$linkdir" | wc -l)" -eq "$(echo "$objs" | wc -w)" ] || {
        echo "error: two $name objects flattened to the same name in $linkdir" >&2; exit 1; }
    local a extra=()
    for a in $forceload; do extra+=("-Wl,-force_load,$BUILD/$a"); done
    for a in $archives; do extra+=("$BUILD/$a"); done
    fs_link_exe "$STAGE/usr/X11/bin/$name" "$linkdir" \
        "${extra[@]}" "$GEN/md/libmd.a" "${SERVER_DYLIBS[@]}" "$@"
}
build_server hw/vfb/Xvfb Xvfb
build_server hw/xfree86/Xorg Xorg "$OUT/system/libxcvt.dylib"
# Xorg's input driver xf86-input-iohid (DAR-391) opens IOHIDFamily user
# clients from inside this process, and both check the task's entitlements:
# IOHIDEventServiceUserClient type 'esuc' wants
# com.apple.hid.system.user-access-service, IOHIDSystem's server connection
# (type 0, used to take keys from the text console) wants
# com.apple.hid.system.server-access. The kernel reads entitlements only from
# a CS_SIGNED process (xnu kern_cs.c:1463-1469), which the amfi_standin MACF
# policy grants to any ad-hoc signature (DAR-391; real signing is DAR-414).
codesign -s - -f --entitlements x11_config/Xorg.entitlements "$STAGE/usr/X11/bin/Xorg" 2>&1 \
    | grep -v "replacing existing signature" || true
for e in com.apple.hid.system.server-access com.apple.hid.system.user-access-service; do
    codesign -d --entitlements - "$STAGE/usr/X11/bin/Xorg" 2>/dev/null | grep -q "$e" || {
        echo "error: Xorg is not signed with $e" >&2; exit 1; }
done
mkdir -p "$STAGE/usr/X11/share/xkeyboard-config-2/compiled"
