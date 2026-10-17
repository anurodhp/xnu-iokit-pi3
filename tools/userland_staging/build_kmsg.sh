#!/bin/bash
# Builds /usr/sbin/kmsg (kmsg.c): prints the kernel message buffer, like
# dmesg(8). A dynamic executable against libSystem only, gated by
# bind_target_audit.sh.
set -euo pipefail
cd "$(dirname "$0")"

OUT="libc_build"
T="../../third_party"
XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
NEWLD_BINDIR="$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain/usr/bin"
S="$OUT/system"

for f in libsystem_kernel libsystem_c libsystem_malloc libdyld libSystem.B; do
    [ -f "$S/$f.dylib" ] || { echo "error: $S/$f.dylib not found -- build it first" >&2; exit 1; }
done

M="$OUT/kmsg.macho"
rm -f "$M"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -O1 -Wall -fno-stack-protector -D_FORTIFY_SOURCE=0 \
    -nostdlib -B"$NEWLD_BINDIR" -Wl,-fixup_chains -DCRT_DYNAMIC_LINKING \
    -o "$M" "$T/Csu/start.s" "$T/Csu/crt.c" kmsg.c \
    "$S/libsystem_c.dylib" "$S/libsystem_malloc.dylib" "$S/libsystem_kernel.dylib" \
    "$S/libdyld.dylib" "$S/libSystem.B.dylib"

otool -l "$M" | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || { echo "error: no LC_LOAD_DYLINKER" >&2; exit 1; }
otool -l "$M" | grep -q "cmd LC_MAIN" || { echo "error: no LC_MAIN" >&2; exit 1; }
./bind_target_audit.sh "$M" || { echo "error: $M has an unresolvable two-level bind" >&2; exit 1; }
codesign -s - -f "$M" 2>&1 | grep -v "replacing existing signature" || true
echo "wrote $M"
