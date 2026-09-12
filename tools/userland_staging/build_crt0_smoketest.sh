#!/bin/bash
# Builds crt0_smoketest.macho using Apple's real, unmodified Csu crt0.o
# (third_party/Csu, static/no-dyld build path) instead of a hand-written
# LC_UNIXTHREAD entry point like tools/pwtool/pwtool.c uses (the retired
# tools/init_binary/init.c used this same hand-written-entry pattern
# too, see docs/static-linking-cleanup-plan.md). Proves Csu's
# real start.s/crt.c work unmodified on this project's toolchain before
# committing to using it for the eventual real-libc userland (Phase 4).
#
# No libc linked in yet -- Phase 4's Libc build is separate, larger,
# unfinished work (see docs/roadmap.md Phase 4 and project memory
# project_xnu_rpi3_userspace_libc_dyld.md). main() below only issues raw
# syscalls directly, same as tools/pwtool/pwtool.c does, to isolate
# "does crt0's _start -> main() -> exit() handoff work" from "does a full
# libSystem build work."
set -e
cd "$(dirname "$0")"

CSU="../../third_party/Csu"
if [ ! -d "$CSU" ]; then
    echo "error: $CSU not found -- run ./setup_third_party.sh first" >&2
    exit 1
fi

# Xcode-12's iPhoneOS SDK, arm64-apple-ios14.4 -- same toolchain every
# other userland_staging script uses, NOT the live/daily-driver Xcode's
# native-macOS target this script used before. Found 2026-09-11/12
# (DAR-302): targeting arm64-apple-macos11 through the live Xcode makes
# ld64 reject `-U _pthread_main_thread_bootstrap` outright in a fully
# `-static` link and fail with "symbol(s) not found" even though the
# flag reaches ld64 correctly (confirmed via `-v`) -- a real, empirically
# confirmed platform-specific ld64 behavior difference, not a flag typo.
# The identical `-U` flag on the identical symbol works immediately once
# the target/toolchain matches build_libplatform.sh's own precedent.
XCODE12="/Applications/Xcode-12.app/Contents/Developer"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin \
    -fno-stack-protector -O0 -Wall crt0_smoketest.c -o smoketest.o

# -Wl,-U,_pthread_main_thread_bootstrap: real, honest expression of the
# weak_import third_party/Csu's crt.c gained 2026-09-01 (commit 5697d90,
# DAR-164 -- crt.c bootstraps pthread TSD before the __mod_init_func
# loop, for any consumer that provides that symbol). weak_import is a
# dylib-binding concept, so a fully `-static` link rejects it outright
# unless the symbol is either really defined or explicitly declared
# undefined here -- and this smoketest deliberately links no libc at
# all yet (see header comment), so it can never provide it. crt.c's own
# `if (pthread_main_thread_bootstrap)` null guard is what makes leaving
# it unresolved correct rather than merely tolerated -- same precedent
# already established in build_libplatform.sh's own smoketest.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
    -Wl,-U,_pthread_main_thread_bootstrap \
    -o smoketest.macho "$CSU/start.s" "$CSU/crt.c" smoketest.o

echo "wrote smoketest.macho"
otool -l smoketest.macho | grep -q LC_LOAD_DYLIB && \
    echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
otool -l smoketest.macho | grep -q LC_UNIXTHREAD || \
    echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
# _pthread_main_thread_bootstrap is the ONE deliberately-unresolved
# symbol here (crt.c's optional weak_import hook, -U'd above) -- filtered
# out so this check keeps flagging real, unintended gaps instead of
# always firing.
if nm -u smoketest.macho | grep -v '^_pthread_main_thread_bootstrap$' | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u smoketest.macho | grep -v '^_pthread_main_thread_bootstrap$' >&2
fi
