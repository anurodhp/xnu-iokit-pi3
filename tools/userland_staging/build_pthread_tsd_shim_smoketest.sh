#!/bin/bash
# Builds pthread_tsd_shim_smoketest.macho: proves pthread_tsd_shim.c's
# real-named pthread_key_create/setspecific/getspecific (plus the private
# underscore-prefixed aliases) work standalone, independent of the
# deeper Libc locale/stdio chain (see build_libc_locale.sh for that).
# Real host clang / macOS target is fine here (unlike build_libc_locale.sh) -
# this file has zero Libc/system header dependency to trigger the
# SDK-skew problem that requires Xcode 12's iPhoneOS SDK elsewhere.
set -e
cd "$(dirname "$0")"
mkdir -p libc_build

clang -target arm64-apple-macos11 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 -Wall \
    pthread_tsd_shim.c -o libc_build/pthread_tsd_shim_standalone.o
clang -target arm64-apple-macos11 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 -Wall \
    pthread_tsd_shim_smoketest.c -o pthread_tsd_shim_smoketest.o

clang -target arm64-apple-macos11 -nostdlib -static -e start \
    -o pthread_tsd_shim_smoketest.macho \
    ../../third_party/Csu/start.s ../../third_party/Csu/crt.c \
    pthread_tsd_shim_smoketest.o libc_build/pthread_tsd_shim_standalone.o

echo "wrote pthread_tsd_shim_smoketest.macho"
otool -l pthread_tsd_shim_smoketest.macho | grep -q LC_LOAD_DYLIB && \
    echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
otool -l pthread_tsd_shim_smoketest.macho | grep -q LC_UNIXTHREAD || \
    echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
if nm -u pthread_tsd_shim_smoketest.macho | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u pthread_tsd_shim_smoketest.macho >&2
fi
