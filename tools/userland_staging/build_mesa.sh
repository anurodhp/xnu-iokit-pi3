#!/bin/bash
# Mesa 25.2.8 software OpenGL: /usr/X11/lib/libGL.1.dylib (GLX over Xlib,
# gallium softpipe). docs/gpu-opengl-plan.md, Tier A.
#
# Why -Dglx=xlib: it is Mesa's own libGL that renders into the client's
# X drawable through Xlib (XPutImage / XShm) with a gallium softpipe
# context. meson.build:559-566 requires softpipe/llvmpipe for it and
# forbids any DRI driver, so no libdrm, no DRI loader, no server-side GLX
# and no kernel GPU interface are involved. The X server needs nothing
# new for it. (Indirect/server GLX is NOT provided; `glxinfo` shows
# direct rendering = Yes because the library renders locally.)
#
# Same shape as build_xserver.sh: run the project's own meson build as a
# cross build against this project's staged headers/dylibs (pkg-config
# files generated into $GEN/pkgconfig), -nostdlib, probes linked against
# this system's dylibs rather than the SDK's libSystem. C++ (the GLSL
# compiler) uses the project's libc++ headers (-nostdinc++, as
# build_llvm_libcxx.sh) and libc++.dylib.
set -e
cd "$(dirname "$0")"
. ./fontstack_common.sh

SRC="$TP/mesa"
x11_require_source "$SRC/meson.build"
grep -q "version : '25.2.8'" "$SRC/meson.build" || grep -q "25.2.8" "$SRC/VERSION" || {
    echo "error: mesa is not 25.2.8 -- recheck this script" >&2; exit 1; }
for t in meson ninja pkg-config; do
    command -v "$t" >/dev/null || { echo "error: $t not found on the host" >&2; exit 1; }
done
# Mesa's GLSL parser needs bison >= 2.4 (macOS ships 2.3); Homebrew's
# keg-only bison@3.8 / bison. Host tool, not shipped.
for b in /opt/homebrew/opt/bison/bin /opt/homebrew/opt/bison@3.8/bin; do
    [ -x "$b/bison" ] && PATH="$b:$PATH"
done
python3 -c "import mako, yaml, packaging" 2>/dev/null || {
    echo "error: host python needs mako, pyyaml, packaging (pip3 install mako pyyaml packaging)" >&2; exit 1; }
x11_require_staged X11/Xlib.h build_libX11.sh
for d in libX11 libXext libxcb libxcb-randr libz libexpat libc++ libsystem_m; do
    [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- build it first" >&2; exit 1; }
done
x11_require_sys_dylibs

# MESA_VC4=1: also build Mesa's vc4 gallium driver and link libGL with
# mesa_xlib_vc4/xlib_vc4.c in place of the stock xlib.c, so libGL renders on
# the VideoCore IV through libdrm.2.dylib (the shim) when the BCM2837V3D
# service exists and falls back to softpipe otherwise. Hardware path: not
# verified on a Pi. Default (unset) is the software-only libGL that is
# QEMU-verified.
VC4="${MESA_VC4:-0}"
SHIM="$PWD/v3d_drm"
VC4X="$PWD/mesa_xlib_vc4"
DRIVERS=softpipe
SUF=""
# MESA_GLES2=enabled: also build the OpenGL ES 2 API (GLES2 contexts through
# GLX_EXT_create_context_es2_profile; Mesa's EGL cannot be built here, see
# docs/gpu-opengl-plan.md).
GLES2="${MESA_GLES2:-disabled}"
[ "$GLES2" = "enabled" ] && SUF="-es2"
if [ "$VC4" = "1" ]; then
    DRIVERS=softpipe,vc4
    SUF="${SUF}-vc4"
    for d in libdrm libiokituser libCoreFoundation; do
        [ -f "$OUT/system/$d.dylib" ] || { echo "error: $OUT/system/$d.dylib not found -- build it first (libdrm: build_libdrm_v3d.sh)" >&2; exit 1; }
    done
fi

GEN="$PWD/$X11_ROOT/gen/mesa$SUF"
BUILD="$PWD/$X11_ROOT/gen/mesa$SUF-build"
INC="$PWD/$X11_INC"
DEST="$PWD/$OUT/mesa/root"
SYS="$PWD/$OUT/system"
rm -rf "$GEN" "$BUILD" "$DEST"
mkdir -p "$GEN/pkgconfig" "$DEST"

# --- pkg-config files for the staged headers/dylibs ---------------------
for pc in "$TP"/xorgproto/*.pc.in; do
    sed -e "s|@prefix@|$INC|g" -e "s|@includedir@|$INC|g" -e "s|@datarootdir@|$INC|g" \
        -e "s|@datadir@|$INC|g" -e "s|@exec_prefix@|$INC|g" -e "s|@libdir@|$INC|g" \
        -e "s|@PACKAGE_VERSION@|1.6.0|g" "$pc" > "$GEN/pkgconfig/$(basename "${pc%.in}")"
done
# lib_pc <name> <version> <dylib-stem> [Requires]
lib_pc() {
    printf 'Name: %s\nDescription: %s (staged)\nVersion: %s\nCflags: -I%s\nLibs: %s/%s.dylib\n' \
        "$1" "$1" "$2" "$INC" "$SYS" "$3" > "$GEN/pkgconfig/$1.pc"
}
lib_pc x11 1.8.12 libX11
lib_pc xext 1.3.6 libXext
lib_pc xcb 1.17.0 libxcb
lib_pc xcb-randr 1.17.0 libxcb-randr
lib_pc zlib 1.2.11 libz
printf 'Name: expat\nDescription: expat\nVersion: 2.5.0\nCflags: -I%s\nLibs: %s/libexpat.dylib\n' \
    "$PWD/$FS_SYSINC" "$SYS" > "$GEN/pkgconfig/expat.pc"
sed -i '' "s|^Cflags: -I$INC|Cflags: -I$INC -I$PWD/$FS_SYSINC|" "$GEN/pkgconfig/zlib.pc"

# --- cross file ---------------------------------------------------------
TC="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin"
LIBCXX="$PWD/$TP/llvm-project/libcxx"
LIBCXXABI="$PWD/$TP/llvm-project/libcxxabi"
SYSLIBS=("$OUT/system/libinfo.dylib" "${X11_SYS_DYLIBS[@]}" "$OUT/system/libsystem_m.dylib" "$OUT/system/libc++.dylib")
if [ "$VC4" = "1" ]; then
    # the mmap/munmap hook (v3d_mmap_hook.h) reaches every Mesa object, so
    # every link needs the shim, and through it libiokituser + CoreFoundation
    SYSLIBS=("$OUT/system/libdrm.dylib" "$OUT/system/libiokituser.dylib" "$OUT/system/libCoreFoundation.dylib" "${SYSLIBS[@]}")
fi
{
    echo "[binaries]"
    echo "c = ['$CLANG', '-isysroot', '$SDK', '-target', 'arm64-apple-ios14.4']"
    echo "cpp = ['$TC/clang++', '-isysroot', '$SDK', '-target', 'arm64-apple-ios14.4']"
    echo "objc = ['$CLANG', '-isysroot', '$SDK', '-target', 'arm64-apple-ios14.4']"
    echo "ar = '$TC/ar'"
    echo "strip = '$TC/strip'"
    echo "pkg-config = '$(command -v pkg-config)'"
    echo "[built-in options]"
    COMMON="'$X11_OPT', '-fno-builtin', '-fno-stack-protector', '-fno-common', '-D_FORTIFY_SOURCE=0', '-Wno-nullability-completeness', '-Wno-deprecated-declarations', '-I$INC', '-I$PWD/$FS_SYSINC'"
    # Mesa only looks for libdrm on Linux-like systems, so the shim's headers
    # come in as plain flags; the force-included hook redirects Mesa's mmap()
    # of vc4 buffer objects to the shim (no Mesa source changes).
    if [ "$VC4" = "1" ]; then
        COMMON="$COMMON, '-I$SHIM/include', '-I$PWD/$TP/mesa/include', '-include', '$SHIM/include/v3d_mmap_hook.h'"
    fi
    echo "c_args = [$COMMON]"
    echo "cpp_args = [$COMMON, '-nostdinc++', '-isystem', '$LIBCXX/include', '-I$LIBCXXABI/include', '-fexceptions', '-frtti']"
    printf "c_link_args = ['-nostdlib', '-B%s', '-Wl,-fixup_chains', '-Wl,-not_for_dyld_shared_cache'" "$NEWLD_BINDIR"
    for d in "${SYSLIBS[@]}"; do printf ", '%s'" "$PWD/$d"; done
    echo "]"
    printf "cpp_link_args = ['-nostdlib', '-B%s', '-Wl,-fixup_chains', '-Wl,-not_for_dyld_shared_cache'" "$NEWLD_BINDIR"
    for d in "${SYSLIBS[@]}"; do printf ", '%s'" "$PWD/$d"; done
    echo "]"
    echo "[properties]"
    echo "pkg_config_libdir = '$GEN/pkgconfig'"
    echo "needs_exe_wrapper = true"
    echo "[host_machine]"
    echo "system = 'darwin'"
    echo "cpu_family = 'aarch64'"
    echo "cpu = 'arm64'"
    echo "endian = 'little'"
} > "$GEN/cross.ini"

meson setup "$BUILD" "$SRC" --cross-file "$GEN/cross.ini" \
    --prefix=/usr/X11 --buildtype=plain \
    -Dplatforms=x11 -Dglx=xlib -Dgallium-drivers=$DRIVERS -Dvulkan-drivers= \
    -Dllvm=disabled -Dgallium-rusticl=false -Dopengl=true -Dgles1=disabled -Dgles2=$GLES2 \
    -Degl=disabled -Dgbm=disabled -Dglvnd=disabled -Dshared-glapi=enabled \
    -Dlmsensors=disabled -Dlibunwind=disabled -Dvalgrind=disabled -Dzstd=disabled \
    -Dxmlconfig=disabled -Dbuild-tests=false -Dtools= -Dbuild-aco-tests=false \
    -Dglx-direct=false -Dxlib-lease=disabled \
    > "$GEN/meson-setup.log" 2>&1 || { tail -40 "$GEN/meson-setup.log" >&2; exit 1; }
echo "meson setup ok"

# --- compile -----------------------------------------------------------------
# meson's own link already runs through the cross file's -nostdlib link args
# against this project's dylibs, so unlike build_xserver.sh no hand link.
ninja -C "$BUILD" > "$GEN/ninja.log" 2>&1 || {
    grep -E 'error:|FAILED' "$GEN/ninja.log" | head -40 >&2; exit 1; }
# An implicit declaration truncates a pointer return on arm64 (DAR-377).
if grep -qE 'implicit declaration|implicitly declaring' "$GEN/ninja.log"; then
    grep -E 'implicit declaration|implicitly declaring' "$GEN/ninja.log" | sort -u >&2
    echo "error: implicit function declarations in the mesa build (above)" >&2; exit 1
fi

# --- vc4: relink libGL with the hardware-capable screen hook -----------------
# Meson links the stock softpipe-only libGL; with vc4 enabled the same link
# line is replayed with mesa_xlib_vc4/xlib_vc4.c.o in place of xlib.c.o and
# the vc4 driver, the libdrm shim and the debug-hook stubs added.
if [ "$VC4" = "1" ]; then
    (cd "$BUILD" && ninja src/gallium/drivers/vc4/libvc4.a > "$GEN/ninja-vc4.log" 2>&1) || {
        grep -E 'error:|FAILED' "$GEN/ninja-vc4.log" | head -20 >&2; exit 1; }
    python3 - "$BUILD" "$VC4X" "$OUT" "$SHIM" "$GEN" <<'PY'
import json, os, shlex, subprocess, sys
build, vc4x, out, shim, gen = sys.argv[1:6]
tgt = "src/gallium/targets/libgl-xlib/libGL.1.dylib"
def cmds(t):
    return subprocess.check_output(["ninja", "-C", build, "-t", "commands", t]).decode().strip().splitlines()
# compile xlib_vc4.c and the stubs with the flags meson used for xlib.c
cc = next(l for l in cmds(tgt + ".p/xlib.c.o") if "xlib.c" in l)
a = shlex.split(cc)
def compile_one(src, obj):
    args = []
    i = 0
    while i < len(a):
        x = a[i]
        if x in ("-o", "-MF", "-MQ"):
            i += 2
            continue
        if x == "-MD" or x == "-c":
            i += 1
            continue
        if x.endswith("xlib.c") and not x.startswith("-"):
            i += 1
            continue
        if x.endswith("xlib.c.o"):
            i += 1
            continue
        args.append(x)
        i += 1
    args += ["-I" + os.path.join(shim, "include"), "-I" + shim,
             "-I" + os.path.join(os.path.dirname(shim), "..", "..", "third_party", "mesa", "src", "gallium", "drivers"),
             "-I" + os.path.join(os.path.dirname(shim), "..", "..", "third_party", "mesa", "src", "gallium", "winsys"),
             "-I" + os.path.join(os.path.dirname(shim), "..", "..", "third_party", "mesa", "src", "gallium", "include"),
             "-Wno-error", "-Wno-missing-prototypes", "-c", src, "-o", obj]
    subprocess.check_call(args, cwd=build)
o1 = os.path.join(gen, "xlib_vc4.o")
o2 = os.path.join(gen, "mesa_vc4_stubs.o")
compile_one(os.path.join(vc4x, "xlib_vc4.c"), o1)
compile_one(os.path.join(vc4x, "mesa_vc4_stubs.c"), o2)
# replay the link
link = cmds(tgt)[-1]
l = shlex.split(link)
res = []
for x in l:
    if x.endswith("xlib.c.o"):
        res += [o1, o2]
        continue
    res.append(x)
# add vc4 + the shim and its IOKit chain right after the glapi archive
idx = max(i for i, x in enumerate(res) if x.endswith("libglapi.a"))
syslib = os.path.abspath(os.path.join(out, "system"))
extra = [os.path.join(build, "src/gallium/drivers/vc4/libvc4.a")]   # libdrm & co are already on the link line
res[idx + 1:idx + 1] = extra
subprocess.check_call(res, cwd=build)
print("relinked libGL with the vc4 screen hook")
PY
fi

# --- stage ---------------------------------------------------------------------
# Install name /usr/X11/lib/libGL.1.dylib like every X library here
# (x11_common.sh header); meson links it as @rpath/libGL.1.dylib and these
# binaries carry no rpath.
BUILT="$BUILD/src/gallium/targets/libgl-xlib/libGL.1.dylib"
OUTLIB="$OUT/system/libGL.dylib"
cp "$BUILT" "$OUTLIB"
install_name_tool -id "$X11_PREFIX/lib/libGL.1.dylib" "$OUTLIB"
otool -hv "$OUTLIB" | grep -q " DYLIB " || { echo "error: $OUTLIB is not MH_DYLIB" >&2; exit 1; }
# Public headers beside the xorgproto GL/glx*.h already in $X11_INC.
mkdir -p "$X11_INC/GL"
cp "$SRC/include/GL/gl.h" "$SRC/include/GL/glext.h" "$SRC/include/GL/glx.h" \
   "$SRC/include/GL/glxext.h" "$SRC/include/GL/glcorearb.h" "$X11_INC/GL/"
mkdir -p "$X11_INC/KHR"
cp "$SRC/include/KHR/khrplatform.h" "$X11_INC/KHR/"
echo "wrote $OUTLIB ($X11_PREFIX/lib/libGL.1.dylib)"
./bind_target_audit.sh "$OUTLIB"

# GLES2 API library. Mesa links it against libGL (it uses libGL's dispatch
# table), so an application creates an ES2-profile context with
# glXCreateContextAttribsARB and links libGLESv2 + libGL.
if [ "$GLES2" = "enabled" ]; then
    ES2="$OUT/system/libGLESv2.dylib"
    cp "$BUILD/src/mapi/es2api/libGLESv2.2.dylib" "$ES2"
    install_name_tool -id "$X11_PREFIX/lib/libGLESv2.2.dylib" "$ES2"
    install_name_tool -change @rpath/libGL.1.dylib "$X11_PREFIX/lib/libGL.1.dylib" "$ES2"
    mkdir -p "$X11_INC/GLES2" "$X11_INC/GLES3"
    cp "$SRC"/include/GLES2/*.h "$X11_INC/GLES2/"
    cp "$SRC"/include/GLES3/*.h "$X11_INC/GLES3/"
    echo "wrote $ES2 ($X11_PREFIX/lib/libGLESv2.2.dylib)"
    ./bind_target_audit.sh "$ES2" "$OUTLIB" || true
fi
