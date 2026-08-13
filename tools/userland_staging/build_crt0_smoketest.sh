#!/bin/bash
# Builds crt0_smoketest.macho using Apple's real, unmodified Csu crt0.o
# (third_party/Csu, static/no-dyld build path) instead of a hand-written
# LC_UNIXTHREAD entry point like tools/init_binary/init.c uses. Proves Csu's
# real start.s/crt.c work unmodified on this project's toolchain before
# committing to using it for the eventual real-libc userland (Phase 4).
#
# No libc linked in yet -- Phase 4's Libc build is separate, larger,
# unfinished work (see docs/roadmap.md Phase 4 and project memory
# project_xnu_rpi3_userspace_libc_dyld.md). main() below only issues raw
# syscalls directly, same as tools/init_binary/init.c does, to isolate
# "does crt0's _start -> main() -> exit() handoff work" from "does a full
# libSystem build work."
set -e
cd "$(dirname "$0")"

CSU="../../third_party/Csu"
if [ ! -d "$CSU" ]; then
    echo "error: $CSU not found -- run ./setup_third_party.sh first" >&2
    exit 1
fi

clang -target arm64-apple-macos11 -c -ffreestanding -fno-builtin \
    -fno-stack-protector -O0 -Wall crt0_smoketest.c -o smoketest.o

clang -target arm64-apple-macos11 -nostdlib -static -e start \
    -o smoketest.macho "$CSU/start.s" "$CSU/crt.c" smoketest.o

echo "wrote smoketest.macho"
otool -l smoketest.macho | grep -q LC_LOAD_DYLIB && \
    echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
otool -l smoketest.macho | grep -q LC_UNIXTHREAD || \
    echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
