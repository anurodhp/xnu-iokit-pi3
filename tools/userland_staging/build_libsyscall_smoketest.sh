#!/bin/bash
# Links real Apple libsyscall syscall stubs (regenerated fresh from
# third_party/xnu-7195's own generator - see the `perl create-syscalls.pl`
# invocation below, output copied into libsyscall_smoketest/ so it survives
# without needing /private/tmp state) against real Csu crt0 and this
# project's minimal cerror stand-in (cerror_stub.c). No libc beyond that.
set -e
cd "$(dirname "$0")"

XNU="../../third_party/xnu-7195"
CSU="../../third_party/Csu"
GENDIR="libsyscall_smoketest"

if [ ! -d "$CSU" ] || [ ! -d "$XNU/libsyscall" ]; then
    echo "error: third_party/Csu or third_party/xnu-7195/libsyscall not found -- run ./setup_third_party.sh first" >&2
    exit 1
fi

# Regenerate the stub .s files fresh from Apple's own generator each build
# (cheap, and keeps this from silently drifting from syscalls.master).
TMPGEN="$(mktemp -d)"
trap 'rm -rf "$TMPGEN"' EXIT
ARCHS=arm64 perl "$XNU/libsyscall/xcodescripts/create-syscalls.pl" \
    "$XNU/bsd/kern/syscalls.master" \
    "$XNU/libsyscall/custom" \
    "$XNU/libsyscall/Platforms" \
    iPhoneOS \
    "$TMPGEN" >/dev/null
cp "$TMPGEN/___getpid.s" "$TMPGEN/_write.s" "$TMPGEN/___exit.s" "$GENDIR/"

CLANG_FLAGS="-target arm64-apple-macos11 -c -arch arm64 -I$XNU/libsyscall/custom -I$XNU/libsyscall/mach"

clang $CLANG_FLAGS "$GENDIR/___getpid.s" -o "$GENDIR/getpid.o"
clang $CLANG_FLAGS "$GENDIR/_write.s" -o "$GENDIR/write.o"
clang $CLANG_FLAGS "$GENDIR/___exit.s" -o "$GENDIR/exit.o"
clang -target arm64-apple-macos11 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 -Wall \
    cerror_stub.c -o cerror_stub.o
clang -target arm64-apple-macos11 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 -Wall \
    libsyscall_smoketest.c -o libsyscall_smoketest.o

clang -target arm64-apple-macos11 -nostdlib -static -e start \
    -o libsyscall_smoketest.macho \
    "$CSU/start.s" "$CSU/crt.c" \
    libsyscall_smoketest.o cerror_stub.o \
    "$GENDIR/getpid.o" "$GENDIR/write.o" "$GENDIR/exit.o"

echo "wrote libsyscall_smoketest.macho"
otool -l libsyscall_smoketest.macho | grep -q LC_LOAD_DYLIB && \
    echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
otool -l libsyscall_smoketest.macho | grep -q LC_UNIXTHREAD || \
    echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
