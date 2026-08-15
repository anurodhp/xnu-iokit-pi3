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

if [ ! -f "$OUT/write.o" ] || [ ! -f "$OUT/exit.o" ] || [ ! -f "$OUT/cerror_stub.o" ]; then
    echo "error: $OUT/{write,exit,cerror_stub}.o not found -- run build_libsyscall_smoketest.sh first" >&2
    exit 1
fi

clang -target arm64-apple-macos11 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 -Wall \
    stack_probe.c -o "$OUT/stack_probe.o"

clang -target arm64-apple-macos11 -nostdlib -static -e start \
    -o stack_probe.macho \
    "$CSU/start.s" "$CSU/crt.c" \
    "$OUT/stack_probe.o" "$OUT/cerror_stub.o" "$OUT/write.o" "$OUT/exit.o"

echo "wrote stack_probe.macho"
otool -l stack_probe.macho | grep -q LC_LOAD_DYLIB && \
    echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
otool -l stack_probe.macho | grep -q LC_UNIXTHREAD || \
    echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
if nm -u stack_probe.macho | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u stack_probe.macho >&2
fi
