#!/bin/bash
# Builds tsd_bootstrap.macho: the minimal real TSD bootstrap + smoketest
# (platform_syscall "set/get cthread self" + an adapted pthread_key_create/
# setspecific/getspecific) - see tsd_bootstrap.c's own header comment for
# the full ABI trace and what's real Apple mechanism vs. a documented
# simplification.
#
# NOTE ON VERIFICATION: this links cleanly and (verified) has zero
# undefined symbols beyond the one deliberately-deferred
# _pthread_main_thread_bootstrap (crt.c's optional weak_import hook --
# see build_libplatform.sh's own smoketest for the full precedent this
# follows) / stays a static, dyld-free LC_UNIXTHREAD Mach-O - but
# it CANNOT be executed directly on a real macOS host to prove the actual
# platform_syscall round-trip: even the simplest possible freestanding
# static binary from this project (tools/userland_staging/crt0_smoketest.c,
# no platform_syscall involved at all) gets killed (exit 137) by this
# host's own hardened-runtime/AMFI enforcement before main() ever runs -
# confirmed by testing that exact file, not assumed. This is a real,
# structural limitation of the whole userland_staging approach on a modern
# host, not a signal about this file's correctness. Real functional
# verification (does TPIDRRO_EL0 actually get set, does a value actually
# round-trip) needs this project's own QEMU/kernel environment - inject
# this .macho into the real SD image the way
# tools/init_binary/inject_into_sd_image.sh does for launchd.macho/
# pwtool.macho, or run it standalone under QEMU with -kernel/-initrd.
set -e
cd "$(dirname "$0")"

# Xcode-12/iPhoneOS, not the live Xcode's native-macOS target: linking
# Csu's crt.c through the live Xcode makes ld64 reject
# -Wl,-U,_pthread_main_thread_bootstrap outright in a fully -static
# link (same real bug class as DAR-302's build_crt0_smoketest.sh fix,
# confirmed there, not re-derived here).
XCODE12="/Applications/Xcode-12.app/Contents/Developer"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 -Wall \
    tsd_bootstrap.c -o tsd_bootstrap.o

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
    -Wl,-U,_pthread_main_thread_bootstrap \
    -o tsd_bootstrap.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    tsd_bootstrap.o

echo "wrote tsd_bootstrap.macho"
otool -l tsd_bootstrap.macho | grep -q LC_LOAD_DYLIB && \
    echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
otool -l tsd_bootstrap.macho | grep -q LC_UNIXTHREAD || \
    echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
if nm -u tsd_bootstrap.macho | grep -v '^_pthread_main_thread_bootstrap$' | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u tsd_bootstrap.macho | grep -v '^_pthread_main_thread_bootstrap$' >&2
fi
