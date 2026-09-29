#!/bin/bash
# Builds kqueue_poll_smoketest.macho (DAR-417): does poll() on a kqueue fd
# report readiness correctly? See kqueue_poll_smoketest.c. Dynamic, linked
# like build_hid_event_smoketest.sh, and installed by inject_into_sd_image.sh
# as /usr/local/bin/kqueue_poll_smoketest.
set -euo pipefail
cd "$(dirname "$0")"
OUT="libc_build"
T="../../third_party"
XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
NEWLD_BINDIR="$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain/usr/bin"
S="$OUT/system"
for f in libsystem_kernel libsystem_c libsystem_pthread libsystem_malloc libsystem_platform libdyld libSystem.B; do
    [ -f "$S/$f.dylib" ] || { echo "error: $S/$f.dylib not found -- build it first" >&2; exit 1; }
done
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -O0 -Werror=implicit-function-declaration \
    kqueue_poll_smoketest.c -o "$OUT/kqueue_poll_smoketest.o"
M="$OUT/kqueue_poll_smoketest.macho"
rm -f "$M"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o "$M" -DCRT_NO_PTHREAD_BOOTSTRAP "$T/Csu/start.s" "$T/Csu/crt.c" "$OUT/kqueue_poll_smoketest.o" \
    "$S/libsystem_kernel.dylib" "$S/libsystem_c.dylib" "$S/libsystem_pthread.dylib" \
    "$S/libsystem_malloc.dylib" "$S/libsystem_platform.dylib" "$S/libdyld.dylib" "$S/libSystem.B.dylib"
otool -l "$M" | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || { echo "error: no LC_LOAD_DYLINKER" >&2; exit 1; }
otool -l "$M" | grep -q "cmd LC_MAIN" || { echo "error: no LC_MAIN" >&2; exit 1; }
./bind_target_audit.sh "$M" || { echo "error: $M has an unresolvable two-level bind" >&2; exit 1; }
echo "wrote $M"
