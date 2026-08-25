#!/bin/bash
# Phase 6.5 Phase B (docs/daemon-roadmap.md): builds dyld_stub.macho, a
# throwaway stand-in for /usr/lib/dyld -- see dyld_stub_main.c's header for
# why this exists (kernel-plumbing proof, not real dyld).
#
# Uses `-dylinker -dylinker_install_name /usr/lib/dyld` instead of
# `-static` to get a real MH_DYLINKER (per `man ld`, Xcode-12's ld64:
# "-dylinker: Produce a mach-o dylinker that has file type MH_DYLINKER.
# Only used when building dyld.") -- confirmed experimentally this session
# that's the only flag pair needed, and third_party/xnu-7195's
# load_dylinker() (mach_loader.c) loads the result via the same
# parse_machfile() path as any other image.
#
# Uses a custom entry (dyld_stub_start.s), NOT third_party/Csu's start.s/
# crt.c -- an earlier version of this script used Csu directly and hit a
# real, root-caused bug: whenever load_result.dynlinker is true, the kernel
# (bsd/kern/kern_exec.c) always pushes an extra mach_header pointer below
# argc on the new stack (the real ABI real dyld's own dyldStartup.s
# expects), and Csu's start.s has no idea that slot exists, silently
# misreading it as argc. See dyld_stub_start.s's own header comment for the
# full writeup.
set -e
cd "$(dirname "$0")"

XCODE12="/Applications/Xcode-12.app/Contents/Developer"
CLANG="$XCODE12/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
SDK="$XCODE12/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"
if [ ! -x "$CLANG" ] || [ ! -d "$SDK" ]; then
    echo "error: Xcode-12.app not found at $XCODE12 (see tools/build_kexts.sh for the same dependency)" >&2
    exit 1
fi

"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -c -ffreestanding \
    -fno-builtin -fno-stack-protector -O0 -Wall dyld_stub_main.c -o dyld_stub_main.o

rm -f dyld_stub.macho
"$CLANG" -isysroot "$SDK" -target arm64-apple-ios14.4 -nostdlib \
    -Wl,-dylinker -Wl,-dylinker_install_name,/usr/lib/dyld \
    -o dyld_stub.macho \
    dyld_stub_start.s dyld_stub_main.o
rm -f dyld_stub_main.o

echo "wrote dyld_stub.macho"
otool -hv dyld_stub.macho | grep -q " DYLINKER " || {
    echo "error: filetype is not DYLINKER -- kernel's load_dylinker() requires MH_DYLINKER" >&2
    exit 1
}
echo "confirmed: dyld_stub.macho is a real MH_DYLINKER"
if nm -u dyld_stub.macho | grep -q .; then
    echo "WARNING: undefined symbols present:" >&2
    nm -u dyld_stub.macho >&2
fi
