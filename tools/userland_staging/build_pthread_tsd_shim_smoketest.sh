#!/bin/bash
# Builds pthread_tsd_shim_smoketest.macho: proves pthread_tsd_shim.c's
# real-named pthread_key_create/setspecific/getspecific (plus the private
# underscore-prefixed aliases) work standalone, independent of the
# deeper Libc locale/stdio chain (see build_libc_locale.sh for that).
#
# Still needs Xcode-12/iPhoneOS despite having zero Libc/system header
# dependency: linking Csu's crt.c through the live Xcode's native-macOS
# target makes ld64 reject -Wl,-U,_pthread_main_thread_bootstrap
# outright in a fully -static link (same real bug class as DAR-302's
# build_crt0_smoketest.sh fix, confirmed there, not re-derived here).
# The header-skew problem this comment used to describe is a different,
# narrower issue and doesn't make this script exempt from that one.
set -e
cd "$(dirname "$0")"
mkdir -p libc_build

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 -Wall \
    pthread_tsd_shim.c -o libc_build/pthread_tsd_shim_standalone.o
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 -Wall \
    pthread_tsd_shim_smoketest.c -o pthread_tsd_shim_smoketest.o

# -Wl,-U,_pthread_main_thread_bootstrap: same real, honest deferral
# build_libplatform.sh's own smoketest already establishes -- this
# smoketest deliberately links no libc beyond Csu's crt0, so it can
# never provide the symbol crt.c's optional weak_import hook looks for.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
    -Wl,-U,_pthread_main_thread_bootstrap \
    -o pthread_tsd_shim_smoketest.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    pthread_tsd_shim_smoketest.o libc_build/pthread_tsd_shim_standalone.o

echo "wrote pthread_tsd_shim_smoketest.macho"
otool -l pthread_tsd_shim_smoketest.macho | grep -q LC_LOAD_DYLIB && \
    echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
otool -l pthread_tsd_shim_smoketest.macho | grep -q LC_UNIXTHREAD || \
    echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
if nm -u pthread_tsd_shim_smoketest.macho | grep -v '^_pthread_main_thread_bootstrap$' | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u pthread_tsd_shim_smoketest.macho | grep -v '^_pthread_main_thread_bootstrap$' >&2
fi
