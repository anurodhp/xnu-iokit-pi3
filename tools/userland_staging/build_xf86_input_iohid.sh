#!/bin/bash
# Builds the Xorg input driver iohid_drv.so (staged as
# libc_build/x11/server/root/usr/X11/lib/xorg/modules/input/iohid_drv.so).
# DAR-391.
#
# Source: src/Userspace/xf86-input-iohid/iohid_drv.c in the PureDarwin fork
# (third_party/PureDarwin, branch iokit-launchd-port), extracted with
# `git show iokit-launchd-port:` like build_xf86_video_puredarwingop.sh. It
# reads keyboard and pointer events from IOHIDFamily's
# IOHIDEventServiceUserClient and takes keys from the text console through
# IOHIDSystem's server connection (see the file's header). Both user clients
# check entitlements that build_xserver.sh signs into Xorg.
#
# Compile and link are build_xf86_video_puredarwingop.sh's: the in-tree
# module compile flags (shmodule.c's compile_commands.json entry) plus the
# IOKitLib headers, then an MH_BUNDLE with -bundle_loader Xorg, with its
# <main-executable> binds checked against Xorg's exports. Xorg looks input
# drivers up as "<driver>_drv.so" under <module path>/input
# (hw/xfree86/loader/loadmod.c).
set -euo pipefail
cd "$(dirname "$0")"
OUT="libc_build"
T="../../third_party"
XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
NEWLD_BINDIR="$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain/usr/bin"
S="$OUT/system"
PD_REF="iokit-launchd-port"   # setup_third_party.sh's PureDarwin branch
BUILD="$PWD/$OUT/x11/gen/xserver-build"
XORG="$OUT/x11/server/root/usr/X11/bin/Xorg"
DEST="$OUT/x11/server/root/usr/X11/lib/xorg/modules/input"

for f in libiokituser libCoreFoundation libsystem_kernel libsystem_c libsystem_pthread \
         libsystem_malloc libsystem_platform libdyld libSystem.B; do
    [ -f "$S/$f.dylib" ] || { echo "error: $S/$f.dylib not found -- build it first" >&2; exit 1; }
done
[ -x "$XORG" ] && [ -f "$BUILD/compile_commands.json" ] || { echo "error: Xorg not built -- run build_xserver.sh first" >&2; exit 1; }
[ -d "$OUT/iokitlib_headers" ] || { echo "error: $OUT/iokitlib_headers not found -- run build_libiokituser_dylib.sh first" >&2; exit 1; }
[ -d "$T/PureDarwin/.git" ] || { echo "error: $T/PureDarwin not found -- run setup_third_party.sh first" >&2; exit 1; }

SRC="$PWD/$OUT/iohid_drv_src"
OBJ="$PWD/$OUT/iohid_drv_obj"
rm -rf "$SRC" "$OBJ"
mkdir -p "$SRC" "$OBJ" "$DEST"
git -C "$T/PureDarwin" show "$PD_REF":src/Userspace/xf86-input-iohid/iohid_drv.c > "$SRC/iohid_drv.c"

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
# IOKitLib and CoreFoundation headers come after the server's own, with
# build_pdgop_smoketest.sh's include set (-idirafter for osfmk, whose
# headers would otherwise shadow the SDK's).
I="$PWD/$OUT"
X="$(cd "$T/xnu-7195" && pwd)"
(cd "$BUILD" && eval "$MODCMD" -c -Werror=implicit-function-declaration -Wno-nullability-completeness \
    -DPRIVATE=1 -idirafter "$I/iokitlib_headers" -idirafter "$I/compat_include" -idirafter "$I/cf_headers" \
    -idirafter "$X/iokit" -idirafter "$X/libkern" \
    -idirafter "$X/osfmk" \
    "$SRC/iohid_drv.c" -o "$OBJ/iohid_drv.o")

M="$DEST/iohid_drv.so"
rm -f "$M"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -bundle -nostdlib -B"$NEWLD_BINDIR" \
    -Wl,-fixup_chains -bundle_loader "$XORG" \
    -o "$M" "$OBJ/iohid_drv.o" \
    "$S/libiokituser.dylib" "$S/libCoreFoundation.dylib" "$S/libsystem_kernel.dylib" \
    "$S/libsystem_c.dylib" "$S/libsystem_pthread.dylib" "$S/libsystem_malloc.dylib" \
    "$S/libsystem_platform.dylib" "$S/libdyld.dylib" "$S/libSystem.B.dylib"
otool -hv "$M" | grep -q " BUNDLE " || { echo "error: $M is not MH_BUNDLE" >&2; exit 1; }
nm -gU "$M" | awk '{print $3}' | grep -qx _iohidModuleData || { echo "error: $M does not export _iohidModuleData" >&2; exit 1; }
FLAT=$(nm -m "$M" | grep 'dynamically looked up' || true)
if [ -n "$FLAT" ]; then
    echo "error: $M has flat-namespace references:" >&2
    echo "$FLAT" >&2
    exit 1
fi
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
