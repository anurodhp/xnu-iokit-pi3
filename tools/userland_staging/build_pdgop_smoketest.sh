#!/bin/bash
# Builds pdgop_smoketest.macho (DAR-358): PureDarwin's PDGOP.c, unmodified,
# plus pdgop_smoketest.c, against this port's real IOKitLib
# (libiokituser.dylib, DAR-251) and CoreFoundation. See pdgop_smoketest.c.
#
# PDGOP.c and PDGOP.h come from the PureDarwin fork pin
# (third_party/PureDarwin, branch iokit-launchd-port). That clone is sparse,
# so they are extracted with `git show iokit-launchd-port:<path>` (the
# pinned branch) into
# libc_build/pdgop_src/ rather than read from the working tree.
# Link line: build_iokitlib_smoketest.sh's.
set -euo pipefail
cd "$(dirname "$0")"
OUT="libc_build"
T="../../third_party"
XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
NEWLD_BINDIR="$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain/usr/bin"
S="$OUT/system"
for f in libiokituser libCoreFoundation libsystem_kernel libsystem_c libsystem_pthread \
         libsystem_malloc libsystem_platform libdyld libSystem.B; do
    [ -f "$S/$f.dylib" ] || { echo "error: $S/$f.dylib not found -- build it first" >&2; exit 1; }
done
[ -d "$OUT/iokitlib_headers" ] || { echo "error: $OUT/iokitlib_headers not found -- run build_libiokituser_dylib.sh first" >&2; exit 1; }
[ -d "$T/PureDarwin/.git" ] || { echo "error: $T/PureDarwin not found -- run setup_third_party.sh first" >&2; exit 1; }

SRC="$OUT/pdgop_src"
rm -rf "$SRC"
mkdir -p "$SRC/include"
git -C "$T/PureDarwin" show iokit-launchd-port:src/Libraries/PDGOP/PDGOP.c > "$SRC/PDGOP.c"
git -C "$T/PureDarwin" show iokit-launchd-port:src/Libraries/PDGOP/include/PDGOP.h > "$SRC/include/PDGOP.h"

CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -O0 -Wno-nullability-completeness -DPRIVATE=1
    -Werror=implicit-function-declaration
    -I "$SRC/include" -I "$OUT/iokitlib_headers" -I "$OUT/compat_include" -I "$OUT/cf_headers"
    -I "$T/xnu-7195/iokit" -I "$T/xnu-7195/libkern" -idirafter "$T/xnu-7195/osfmk")
"$CLANG" "${CFLAGS[@]}" "$SRC/PDGOP.c" -o "$OUT/pdgop_PDGOP.o"
"$CLANG" "${CFLAGS[@]}" pdgop_smoketest.c -o "$OUT/pdgop_smoketest.o"

M="$OUT/pdgop_smoketest.macho"
rm -f "$M"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o "$M" "$T/Csu/start.s" "$T/Csu/crt.c" "$OUT/pdgop_smoketest.o" "$OUT/pdgop_PDGOP.o" \
    "$S/libiokituser.dylib" "$S/libCoreFoundation.dylib" "$S/libsystem_kernel.dylib" \
    "$S/libsystem_c.dylib" "$S/libsystem_pthread.dylib" "$S/libsystem_malloc.dylib" \
    "$S/libsystem_platform.dylib" "$S/libdyld.dylib" "$S/libSystem.B.dylib"
otool -l "$M" | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || { echo "error: no LC_LOAD_DYLINKER" >&2; exit 1; }
otool -l "$M" | grep -q "cmd LC_MAIN" || { echo "error: no LC_MAIN" >&2; exit 1; }
./bind_target_audit.sh "$M" || { echo "error: $M has an unresolvable two-level bind" >&2; exit 1; }
echo "wrote $M"
