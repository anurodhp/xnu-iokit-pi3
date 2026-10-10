# Sourced by the build_*.sh in this directory. Same toolchain recipe as
# tools/userland_staging/build_audio_play.sh (Xcode 12 iPhoneOS SDK, arm64-apple-ios14.4,
# direct clang, fixup chains). The CoreAudio public headers come from the Xcode-12 MacOSX SDK and
# are only linked into a build directory, never copied into the repo.
set -euo pipefail
CA_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STAGING="$CA_DIR/../tools/userland_staging"
OUT="$STAGING/libc_build"
T="$CA_DIR/../third_party"
XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
MACSDK_CA="$XCODE12/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/System/Library/Frameworks/CoreAudio.framework/Headers"
NEWLD_BINDIR="$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain/usr/bin"
S="$OUT/system"
need_dylibs() { for f in "$@"; do [ -f "$S/$f.dylib" ] || { echo "error: $S/$f.dylib not found -- build it first" >&2; exit 1; }; done; }
CA_HDRS="$OUT/coreaudio_headers"
mkdir -p "$CA_HDRS"
[ -d "$MACSDK_CA" ] || { echo "error: $MACSDK_CA not found" >&2; exit 1; }
ln -sfn "$MACSDK_CA" "$CA_HDRS/CoreAudio"
COMMON_CFLAGS=(-isysroot "$SDK" -target arm64-apple-ios14.4 -c -O1 -Wall -Wno-nullability-completeness
    -Wno-unused-function -Werror=implicit-function-declaration -DPRIVATE=1
    -I "$CA_DIR/include" -I "$CA_DIR/daemon" -I "$CA_HDRS" -I "$OUT/iokitlib_headers" -I "$OUT/compat_include"
    -I "$OUT/cf_headers" -I "$T/xnu-7195/libkern" -idirafter "$T/xnu-7195/osfmk" -I "$T/xnu-7195/iokit")
EXEC_LIBS=("$S/libsystem_pthread.dylib" "$S/libsystem_c.dylib" "$S/libsystem_malloc.dylib" "$S/libsystem_kernel.dylib"
    "$S/libsystem_platform.dylib" "$S/libdyld.dylib" "$S/libSystem.B.dylib")
check_dynamic_exec() {
    otool -l "$1" | grep -A2 "cmd LC_LOAD_DYLINKER" | grep -q "/usr/lib/dyld" || { echo "error: $1: no LC_LOAD_DYLINKER" >&2; exit 1; }
    otool -l "$1" | grep -q "cmd LC_MAIN" || { echo "error: $1: no LC_MAIN" >&2; exit 1; }
    "$STAGING/bind_target_audit.sh" "$1" || { echo "error: $1 has an unresolvable two-level bind" >&2; exit 1; }
}
