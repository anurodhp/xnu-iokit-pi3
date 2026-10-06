#!/bin/bash
# Builds fb_bench.macho: write bandwidth into the live framebuffer through
# PDGOP, as the puredarwingop X driver's shadow update writes it (DARW-302;
# see fb_bench.c). A bring-up tool, not installed by inject_into_sd_image.sh:
# copy it to the target and run it as root while X is up (it writes the
# current screen back to itself, so nothing visible changes).
#
# PDGOP.c comes from the PureDarwin fork exactly as
# build_xf86_video_puredarwingop.sh takes it (same branch, same flags).
# FB_BENCH_CFLAGS adds compile flags, e.g. -fno-builtin as the X server and
# its driver are built (build_xserver.sh), to compare.
set -e
cd "$(dirname "$0")"
. ./x11_common.sh

T="../../third_party"
PD_REF="${PD_REF:-iokit-launchd-port}"
S="$OUT/system"
OBJ="$X11_ROOT/obj/fb_bench"
rm -rf "$OBJ"
mkdir -p "$OBJ/include"
git -C "$T/PureDarwin" show "$PD_REF":src/Libraries/PDGOP/PDGOP.c > "$OBJ/PDGOP.c"
git -C "$T/PureDarwin" show "$PD_REF":src/Libraries/PDGOP/include/PDGOP.h > "$OBJ/include/PDGOP.h"
for f in PDGOP.c fb_bench.c; do
    src="$OBJ/PDGOP.c"
    [ "$f" = fb_bench.c ] && src=fb_bench.c
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -O2 ${FB_BENCH_CFLAGS:-} -Wno-nullability-completeness -DPRIVATE=1 \
        -Werror=implicit-function-declaration \
        -I "$OBJ/include" -I "$OUT/iokitlib_headers" -I "$OUT/compat_include" -I "$OUT/cf_headers" \
        -I "$T/xnu-7195/iokit" -I "$T/xnu-7195/libkern" -idirafter "$T/xnu-7195/osfmk" \
        "$src" -o "$OBJ/${f%.c}.o"
done
LINK=("$S/libiokituser.dylib" "$S/libCoreFoundation.dylib")
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -DCRT_DYNAMIC_LINKING -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o fb_bench.macho ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    "$OBJ"/*.o "${LINK[@]}" "${X11_SYS_DYLIBS[@]}"
otool -l fb_bench.macho | grep -q "cmd LC_MAIN" || { echo "error: fb_bench: no LC_MAIN" >&2; exit 1; }
./bind_target_audit.sh fb_bench.macho "${LINK[@]}"
echo "wrote fb_bench.macho"
