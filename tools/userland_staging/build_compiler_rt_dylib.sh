#!/bin/bash
# Builds libc_build/system/libcompiler_rt.dylib (install name /usr/lib/system/libcompiler_rt.dylib):
# the 128-bit integer division builtins __udivti3, __umodti3, __divti3, __modti3 (+ __udivmodti4,
# __umodti4's helper). DARW-284: real Darwin's libSystem re-exports libcompiler_rt, so every image
# gets them; here no dylib did (___udivti3 existed only incidentally in libCoreFoundation, and
# Xcode 12's libclang_rt.ios.a leaves both undefined for arm64). GLib's gmain.c time arithmetic
# and WebKit need them.
# Source: LLVM compiler-rt lib/builtins at llvmorg-11.1.0 (third_party/llvm-project, sparse-cloned
# by setup_third_party.sh next to libcxx/libcxxabi), compiled unmodified. libSystem.B.dylib
# re-exports the result (build_libsystem.sh).
set -e
cd "$(dirname "$0")"
OUT="libc_build"
RT="../../third_party/llvm-project/compiler-rt/lib/builtins"
[ -f "$RT/udivti3.c" ] || { echo "error: $RT missing -- run setup_third_party.sh (sparse-checkout needs compiler-rt/lib/builtins)" >&2; exit 1; }
XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
[ -x "$CLANG" ] && [ -d "$SDK" ] || { echo "error: Xcode-12.app not found at $XCODE12" >&2; exit 1; }
NEWLD_BINDIR="$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain/usr/bin"
[ -x "$NEWLD_BINDIR/ld" ] || { echo "error: no usable ld under $NEWLD_BINDIR (docs/backlog.md item d9)" >&2; exit 1; }
W="$OUT/compiler_rt"; rm -rf "$W"; mkdir -p "$W" "$OUT/system"
for f in udivti3 umodti3 divti3 modti3 udivmodti4; do
    "$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O2 \
        -I "$RT" "$RT/$f.c" -o "$W/$f.o"
done
rm -f "$OUT/system/libcompiler_rt.dylib"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -dynamiclib -nostdlib \
    -install_name /usr/lib/system/libcompiler_rt.dylib \
    -Wl,-not_for_dyld_shared_cache -B"$NEWLD_BINDIR" -Wl,-fixup_chains \
    -o "$OUT/system/libcompiler_rt.dylib" "$W"/*.o
otool -hv "$OUT/system/libcompiler_rt.dylib" | grep -q " DYLIB " || { echo "error: filetype is not DYLIB" >&2; exit 1; }
if nm -u "$OUT/system/libcompiler_rt.dylib" | grep -q .; then
    echo "error: libcompiler_rt.dylib has undefined symbols:" >&2; nm -u "$OUT/system/libcompiler_rt.dylib" >&2; exit 1
fi
for s in ___udivti3 ___umodti3 ___divti3 ___modti3; do
    nm -gU "$OUT/system/libcompiler_rt.dylib" | grep -q " $s\$" || { echo "error: $s not exported" >&2; exit 1; }
done
echo "wrote $OUT/system/libcompiler_rt.dylib (zero undefined symbols)"
