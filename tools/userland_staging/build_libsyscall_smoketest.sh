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

# Xcode-12's iPhoneOS SDK, arm64-apple-ios14.4 -- same toolchain every
# other userland_staging script uses, NOT the live/daily-driver Xcode's
# native-macOS target this script used before (same real bug class as
# DAR-302's build_crt0_smoketest.sh fix: targeting arm64-apple-macos11
# through the live Xcode makes ld64 reject
# -Wl,-U,_pthread_main_thread_bootstrap outright in a fully -static
# link, "symbol(s) not found" even with the flag present -- confirmed
# empirically there, not re-derived here, same fix applies).
XCODE12="/Applications/Xcode-12.app/Contents/Developer"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
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

# -idirafter .../EXPORT_HDRS/osfmk: real switch to Xcode-12/iPhoneOS
# exposed a gap the live-Xcode/macos11 target had been silently
# absorbing from its own host SDK -- SYS.h needs mach/arm/syscall_sw.h
# and mach/arm64/asm.h, real xnu headers but only under their real
# namespace-remapped path (mach/arm64/asm.h, not osfmk/arm64/asm.h) --
# these only exist post-export, in the kernel build's own already-built
# EXPORT_HDRS output (tools/build_xnu_7195.sh builds this first), not
# as raw checked-in source anywhere. Plain -idirafter $XNU/osfmk (what
# build_libplatform.sh/build_libsystem_kernel.sh use for headers that
# stay under osfmk/'s own real layout) doesn't reach a remapped path
# like this.
XNU_EXPORT_HDRS="$XNU/BUILD/obj/EXPORT_HDRS/osfmk"
if [ ! -d "$XNU_EXPORT_HDRS" ]; then
    echo "error: $XNU_EXPORT_HDRS not found -- run tools/build_xnu_7195.sh first" >&2
    exit 1
fi
CLANG_FLAGS="-isysroot $SDK -target arm64-apple-ios14.4 -c -arch arm64 -I$XNU/libsyscall/custom -I$XNU/libsyscall/mach -idirafter $XNU_EXPORT_HDRS"

"$CLANG" $CLANG_FLAGS "$GENDIR/___getpid.s" -o "$GENDIR/getpid.o"
"$CLANG" $CLANG_FLAGS "$GENDIR/_write.s" -o "$GENDIR/write.o"
"$CLANG" $CLANG_FLAGS "$GENDIR/___exit.s" -o "$GENDIR/exit.o"
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 -Wall \
    cerror_stub.c -o cerror_stub.o
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding -fno-builtin -fno-stack-protector -O0 -Wall \
    libsyscall_smoketest.c -o libsyscall_smoketest.o

# -Wl,-U,_pthread_main_thread_bootstrap: same real, honest deferral
# build_libplatform.sh's own smoketest and DAR-302's build_crt0_smoketest.sh
# fix already establish -- this smoketest deliberately links no libc
# beyond Csu's crt0, so it can never provide the symbol crt.c's optional
# weak_import hook looks for; crt.c's own null guard is what makes
# leaving it unresolved correct.
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib -static -e start \
    -Wl,-U,_pthread_main_thread_bootstrap \
    -o libsyscall_smoketest.macho \
    "$CSU/start.s" "$CSU/crt.c" \
    libsyscall_smoketest.o cerror_stub.o \
    "$GENDIR/getpid.o" "$GENDIR/write.o" "$GENDIR/exit.o"

echo "wrote libsyscall_smoketest.macho"
otool -l libsyscall_smoketest.macho | grep -q LC_LOAD_DYLIB && \
    echo "WARNING: LC_LOAD_DYLIB present -- no longer dyld-free" >&2
otool -l libsyscall_smoketest.macho | grep -q LC_UNIXTHREAD || \
    echo "WARNING: no LC_UNIXTHREAD -- kernel exec won't know where to start" >&2
# _pthread_main_thread_bootstrap is the ONE deliberately-unresolved
# symbol here (crt.c's optional weak_import hook, -U'd above).
if nm -u libsyscall_smoketest.macho | grep -v '^_pthread_main_thread_bootstrap$' | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u libsyscall_smoketest.macho | grep -v '^_pthread_main_thread_bootstrap$' >&2
fi
