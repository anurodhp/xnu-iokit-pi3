#!/bin/bash
# Builds libc_build/weak_proxy_smoketest.macho (DAR-450): weak references to
# GNUstep NSProxy objects on objc4, see weak_proxy_smoketest.m. Compile flags are
# gnustep-config's own (the installed gnustep-make in
# libc_build/gnustep/root); the link is build_gnustep_base.sh's: our
# dylibs only, owners first, never the SDK's .tbd stubs.
set -euo pipefail
cd "$(dirname "$0")"
OUT="$(cd libc_build && pwd -P)"
G="$OUT/gnustep"
R="$G/root/usr/GNUstep"
CSU="../../third_party/Csu"
XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
DAILY_XCODE="$(xcode-select -p 2>/dev/null)"
NEWLD_BINDIR="$DAILY_XCODE/Toolchains/XcodeDefault.xctoolchain/usr/bin"
SYS="$OUT/system"
GSLIB="$R/System/Library/Libraries/libgnustep-base.dylib"
for f in "$GSLIB" "$G/GNUstep-build.conf" "$OUT/libobjc.A.dylib" "$SYS/libffi.dylib"; do
    [ -e "$f" ] || { echo "error: $f not found -- run build_gnustep_base.sh first" >&2; exit 1; }
done
OBJCFLAGS="$(GNUSTEP_CONFIG_FILE="$G/GNUstep-build.conf" GNUSTEP_MAKEFILES="$R/System/Library/Makefiles" \
             "$R/System/Tools/gnustep-config" --objc-flags | sed 's/-MMD -MP //')"
# shellcheck disable=SC2086 # OBJCFLAGS is a flag list
"$CLANG" $OBJCFLAGS -O0 -D_FORTIFY_SOURCE=0 -I "$R/System/Library/Headers" -I "$OUT/objc4_public_headers" \
    -c weak_proxy_smoketest.m -o "$OUT/weak_proxy_smoketest.o"
# Owners first, libSystem.B last (same order as build_gnustep_base.sh).
LIBS=("$GSLIB" "$SYS/libsystem_kernel.dylib" "$SYS/libsystem_platform.dylib" "$SYS/libsystem_pthread.dylib"
      "$SYS/libsystem_malloc.dylib" "$SYS/libsystem_c.dylib" "$SYS/libsystem_m.dylib"
      "$SYS/libsystem_blocks.dylib" "$SYS/libdispatch.dylib" "$SYS/libdyld.dylib"
      "$SYS/libc++.dylib" "$SYS/libunwind.dylib" "$OUT/libobjc.A.dylib" "$SYS/libSystem.B.dylib")
# libSystem.B's reexports by install path -> our build of each (see
# build_gnustep_base.sh), so ld never reads the SDK's copies.
MAP=()
for p in $(otool -L "$SYS/libSystem.B.dylib" | awk '/reexport/ {print $1}'); do
    f=""
    for c in "$SYS"/*.dylib "$OUT/libobjc.A.dylib"; do
        [ "$(otool -D "$c" 2>/dev/null | tail -1)" = "$p" ] && { f="$c"; break; }
    done
    [ -n "$f" ] || { echo "error: no dylib in $SYS has install name $p" >&2; exit 1; }
    MAP+=("-Wl,-dylib_file,$p:$f")
done
M="$OUT/weak_proxy_smoketest.macho"
rm -f "$M"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -B"$NEWLD_BINDIR" -Wl,-Z -Wl,-fixup_chains \
    "${MAP[@]}" -o "$M" "$CSU/start.s" -DCRT_DYNAMIC_LINKING "$CSU/crt.c" "$OUT/weak_proxy_smoketest.o" "${LIBS[@]}"
otool -l "$M" | grep -q "cmd LC_MAIN" || { echo "error: $M has no LC_MAIN" >&2; exit 1; }
./bind_target_audit.sh "$M"
echo "wrote $M"
