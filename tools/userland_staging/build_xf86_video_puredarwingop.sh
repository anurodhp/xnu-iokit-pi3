#!/bin/bash
# Builds the Xorg video driver puredarwingop_drv.so (staged as
# libc_build/x11/server/root/usr/X11/lib/xorg/modules/drivers/
# puredarwingop_drv.so). DAR-389, X11GS 7.
#
# Source: PureDarwin's src/Userspace/xf86-video-puredarwingop/
# puredarwingop_drv.c plus src/Libraries/PDGOP/PDGOP.c from the PureDarwin
# fork pin (third_party/PureDarwin, branch iokit-launchd-port). The driver
# and PDGOP.c carry fork commits (4b9effb: reopen the framebuffer on server
# regeneration; DAR-439 QoS; DAR-460: VRAMCacheMode/ShadowFB/
# ShadowUpdateDelay/ShadowStats options and PDGOPOpenWithCacheMode, see
# docs/framebuffer-mapping.md). The clone is sparse, so the files are
# extracted with `git show iokit-launchd-port:` (the pinned branch, not
# whatever HEAD happens to be checked out).
# The driver opens the framebuffer through PDGOP (IOFramebufferUserClient
# on this port's IOBootFramebuffer, verified by DAR-358's pdgop_smoketest),
# renders into a shadow buffer and copies damaged areas to VRAM.
#
# Compile: puredarwingop_drv.c uses the exact flags meson recorded for an
# in-tree Xorg module (hw/xfree86/dixmods/shmodule.c's compile_commands.json
# entry, run from the meson build directory build_xserver.sh made), so it
# sees the same dix-config.h/xorg-config.h and headers as the server it
# loads into. PDGOP.c uses build_pdgop_smoketest.sh's IOKitLib flags.
#
# Link: an MH_BUNDLE with -bundle_loader Xorg, which is how meson links
# Xorg's own modules (`link_with: e`, hw/xfree86/dixmods/meson.build) and
# how Darwin plugins normally reach their host: server symbols (fbScreenInit,
# xf86DrvMsg, ...) become two-level binds to the main executable, not
# flat-namespace lookups. (PureDarwin built a flat-namespace dylib instead,
# because its dyld3 could not dlopen an MH_BUNDLE, per its CMakeLists.txt.
# dyld-940 here is tested with a bundle.) IOKit, CoreFoundation and libc
# come from this project's dylibs. The file name is Xorg's
# "<driver>_drv.so" (hw/xfree86/loader/loadmod.c), and it installs under
# the module path /usr/X11/lib/xorg/modules/drivers (meson's module_dir
# default, libdir/xorg/modules).
set -euo pipefail
cd "$(dirname "$0")"
OUT="libc_build"
T="../../third_party"
XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
NEWLD_BINDIR="$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain/usr/bin"
S="$OUT/system"
PD_REF="${PD_REF:-iokit-launchd-port}"   # setup_third_party.sh's PureDarwin branch (override to build a side branch)
BUILD="$PWD/$OUT/x11/gen/xserver-build"
XORG="$OUT/x11/server/root/usr/X11/bin/Xorg"
DEST="$OUT/x11/server/root/usr/X11/lib/xorg/modules/drivers"

for f in libiokituser libCoreFoundation libsystem_kernel libsystem_c libsystem_pthread \
         libsystem_malloc libsystem_platform libdyld libSystem.B; do
    [ -f "$S/$f.dylib" ] || { echo "error: $S/$f.dylib not found -- build it first" >&2; exit 1; }
done
[ -x "$XORG" ] && [ -f "$BUILD/compile_commands.json" ] || { echo "error: Xorg not built -- run build_xserver.sh first" >&2; exit 1; }
[ -d "$OUT/iokitlib_headers" ] || { echo "error: $OUT/iokitlib_headers not found -- run build_libiokituser_dylib.sh first" >&2; exit 1; }
[ -d "$T/PureDarwin/.git" ] || { echo "error: $T/PureDarwin not found -- run setup_third_party.sh first" >&2; exit 1; }

SRC="$PWD/$OUT/pdgop_drv_src"
OBJ="$PWD/$OUT/pdgop_drv_obj"
rm -rf "$SRC" "$OBJ"
mkdir -p "$SRC/include" "$OBJ" "$DEST"
# PDGOP_DRV_SRC=<file>: build the driver from that file instead of the fork
# branch (to test a change on the target before it is merged into the fork).
if [ -n "${PDGOP_DRV_SRC:-}" ]; then
    cp "$PDGOP_DRV_SRC" "$SRC/puredarwingop_drv.c"
    echo "+ driver source from $PDGOP_DRV_SRC (PDGOP_DRV_SRC), not $PD_REF"
else
    git -C "$T/PureDarwin" show "$PD_REF":src/Userspace/xf86-video-puredarwingop/puredarwingop_drv.c > "$SRC/puredarwingop_drv.c"
fi
git -C "$T/PureDarwin" show "$PD_REF":src/Libraries/PDGOP/PDGOP.c > "$SRC/PDGOP.c"
git -C "$T/PureDarwin" show "$PD_REF":src/Libraries/PDGOP/include/PDGOP.h > "$SRC/include/PDGOP.h"

# The in-tree module compile command, retargeted at the driver.
MODCMD=$(cd "$BUILD" && python3 -c '
import json, shlex, sys
for e in json.load(open("compile_commands.json")):
    if e["file"].endswith("hw/xfree86/dixmods/shmodule.c"):
        args = shlex.split(e["command"])
        out, skip = [], False
        for a in args:
            if skip: skip = False; continue
            if a in ("-o", "-MF", "-MQ", "-c"): skip = a != "-c"; continue
            if a in ("-MD",) or a.endswith("shmodule.c"): continue
            out.append(a)
        print(shlex.join(out)); sys.exit(0)
sys.exit("shmodule.c not found in compile_commands.json")')
(cd "$BUILD" && eval "$MODCMD" -c -I "$SRC/include" -Werror=implicit-function-declaration \
    "$SRC/puredarwingop_drv.c" -o "$OBJ/puredarwingop_drv.o")

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c "${X11_OPT:--O2}" -Wno-nullability-completeness -DPRIVATE=1 \
    -Werror=implicit-function-declaration \
    -I "$SRC/include" -I "$OUT/iokitlib_headers" -I "$OUT/compat_include" -I "$OUT/cf_headers" \
    -I "$T/xnu-7195/iokit" -I "$T/xnu-7195/libkern" -idirafter "$T/xnu-7195/osfmk" \
    "$SRC/PDGOP.c" -o "$OBJ/PDGOP.o"

M="$DEST/puredarwingop_drv.so"
rm -f "$M"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -bundle -nostdlib -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains -bundle_loader "$XORG" \
    -o "$M" "$OBJ/puredarwingop_drv.o" "$OBJ/PDGOP.o" \
    "$S/libiokituser.dylib" "$S/libCoreFoundation.dylib" "$S/libsystem_kernel.dylib" \
    "$S/libsystem_c.dylib" "$S/libsystem_pthread.dylib" "$S/libsystem_malloc.dylib" \
    "$S/libsystem_platform.dylib" "$S/libdyld.dylib" "$S/libSystem.B.dylib"
otool -hv "$M" | grep -q " BUNDLE " || { echo "error: $M is not MH_BUNDLE" >&2; exit 1; }
EXPORTS=$(nm -gU "$M" | awk '{print $3}')
for s in _puredarwingopModuleData _PUREDARWINGOP; do
    echo "$EXPORTS" | grep -qx "$s" || { echo "error: $M does not export $s" >&2; exit 1; }
done
FLAT=$(nm -m "$M" | grep 'dynamically looked up' || true)
if [ -n "$FLAT" ]; then
    echo "error: $M has flat-namespace references:" >&2
    echo "$FLAT" >&2
    exit 1
fi
# bind_target_audit.sh only checks binds into dylibs; <main-executable>
# binds (this bundle's calls into Xorg) land in its unchecked bucket. Check
# them here against Xorg's own exports, so a rebuilt Xorg that drops or
# renames a symbol fails this build instead of dlopen on the target.
XORG_EXPORTS=$(nm -gUj "$XORG")
MAINBINDS=$(xcrun dyld_info -fixups "$M" | awk '/<main-executable>\// { sub(/.*<main-executable>\//, ""); print $1 }' | sort -u)
[ -n "$MAINBINDS" ] || { echo "error: no <main-executable> binds found in $M (dyld_info output changed?)" >&2; exit 1; }
MISSING=$(comm -23 <(echo "$MAINBINDS") <(echo "$XORG_EXPORTS" | sort -u))
if [ -n "$MISSING" ]; then
    echo "error: $M binds symbols Xorg does not export:" >&2
    echo "$MISSING" >&2
    exit 1
fi
echo "$(echo "$MAINBINDS" | wc -l | tr -d ' ') <main-executable> binds all resolve in $XORG"
./bind_target_audit.sh "$M" || { echo "error: $M has an unresolvable two-level bind" >&2; exit 1; }
echo "wrote $M"
mkdir -p "$OUT/x11/server/root/etc/X11"
cp x11_config/xorg.conf "$OUT/x11/server/root/etc/X11/xorg.conf"
echo "staged /etc/X11/xorg.conf (x11_config/xorg.conf)"
