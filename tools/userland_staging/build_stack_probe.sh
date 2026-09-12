#!/bin/bash
# Builds stack_probe.macho -- see stack_probe.c's own header comment for
# what this diagnostic tool does and why it's kept. Same minimal
# Csu+real-Apple-generated-syscall-stub link as
# build_libsyscall_smoketest.sh (not the full curated libc_build/*.a
# stack -- this needs nothing beyond write()/_exit()).
set -e
cd "$(dirname "$0")"

CSU="../../third_party/Csu"
OUT="libsyscall_smoketest"
mkdir -p "$OUT"

if [ ! -f "$OUT/write.o" ] || [ ! -f "$OUT/exit.o" ] || [ ! -f "cerror_stub.o" ]; then
    echo "error: $OUT/{write,exit,cerror_stub}.o not found -- run build_libsyscall_smoketest.sh first" >&2
    exit 1
fi

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
    stack_probe.c -o "$OUT/stack_probe.o"

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
    -Wl,-U,_pthread_main_thread_bootstrap \
    -o stack_probe.macho \
    "$CSU/start.s" "$CSU/crt.c" \
    "$OUT/stack_probe.o" "cerror_stub.o" "$OUT/write.o" "$OUT/exit.o"

echo "wrote stack_probe.macho"
otool -l stack_probe.macho | grep -q LC_LOAD_DYLIB && \
    echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
otool -l stack_probe.macho | grep -q LC_UNIXTHREAD || \
    echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
if nm -u stack_probe.macho | grep -v '^_pthread_main_thread_bootstrap$' | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u stack_probe.macho | grep -v '^_pthread_main_thread_bootstrap$' >&2
fi
