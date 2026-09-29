#!/bin/bash
# Builds libc_build/crashdiag.dylib and libc_build/objc_throwdiag.dylib (DAR-416 crash probe, checked in for
# DAR-450; see crashdiag.c). Same real-dylib recipe as
# build_dyld_inject_probe.sh, owners listed before libSystem.B.
set -e
cd "$(dirname "$0")"
OUT="libc_build"
SYS="$OUT/system"
XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
[ -x "$CLANG" ] && [ -d "$SDK" ] || { echo "error: Xcode-12.app not found at $XCODE12" >&2; exit 1; }
[ -f "$SYS/libSystem.B.dylib" ] || { echo "error: run build_libsystem.sh first" >&2; exit 1; }
NEWLD_BINDIR="$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain/usr/bin"
[ -x "$NEWLD_BINDIR/ld" ] || { echo "error: no ld under $NEWLD_BINDIR" >&2; exit 1; }
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib -O1 \
    -install_name /usr/lib/crashdiag.dylib -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o "$OUT/crashdiag.dylib" crashdiag.c \
    "$SYS/libsystem_kernel.dylib" "$SYS/libsystem_c.dylib" "$SYS/libdyld.dylib" "$SYS/libSystem.B.dylib"
otool -l "$OUT/crashdiag.dylib" | grep -Eq "__mod_init_func|__init_offsets" || { echo "error: no constructor section" >&2; exit 1; }
./bind_target_audit.sh "$OUT/crashdiag.dylib"
echo "wrote $OUT/crashdiag.dylib"
# objc_throwdiag.dylib: logs every objc_exception_throw (see objc_throwdiag.c).
[ -f "$OUT/libobjc.A.dylib" ] || { echo "error: run build_objc4.sh first" >&2; exit 1; }
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib -O1 \
    -install_name /usr/lib/objc_throwdiag.dylib -Wl,-not_for_dyld_shared_cache -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o "$OUT/objc_throwdiag.dylib" objc_throwdiag.c \
    "$SYS/libsystem_kernel.dylib" "$SYS/libsystem_c.dylib" "$SYS/libdyld.dylib" "$OUT/libobjc.A.dylib" "$SYS/libSystem.B.dylib"
otool -l "$OUT/objc_throwdiag.dylib" | grep -q "__interpose" || { echo "error: no __interpose section" >&2; exit 1; }
./bind_target_audit.sh "$OUT/objc_throwdiag.dylib"
echo "wrote $OUT/objc_throwdiag.dylib"
