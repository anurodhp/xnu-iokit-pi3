#!/bin/bash
#
# Compiles the project's IOKit kexts (iokit/*/*.cpp) standalone, using
# Xcode 12's clang against third_party/xnu-7195 (see compile_flags.txt).
#
# Xcode 12 is required (not the live/daily-driver Xcode) because its
# iPhoneOS SDK (iOS 14.4 vintage) lines up well with xnu-7195's own
# Darwin 20.3 vintage; even so, its
# xcodebuild/xcrun machinery cannot run at all on this host (macOS 26) -
# it crashes trying to load a plugin that links against
# CoreChart.framework, a private framework Apple has since removed from
# /System/Library/PrivateFrameworks. That only blocks the *IDE* init path
# (xcodebuild build/install, xcrun -find, and mig's wrapper script, which
# internally shells out to `xcrun -find cc`) - the actual command-line
# tools (clang, mig's real migcom, iig) work fine invoked directly.
#
# Prerequisite one-time step if generated/mach or generated/DriverKit are
# ever wiped: regenerate them with mig/iig (see git history of this file's
# commit, or project memory, for the exact invocations - MIGCC=<Xcode-12
# clang path> env var bypasses mig's broken `xcrun -find cc` call).

set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

export DEVELOPER_DIR=/Applications/Xcode-12.app/Contents/Developer
CLANGXX="$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++"
SDK="$DEVELOPER_DIR/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"

if [ ! -x "$CLANGXX" ]; then
	echo "error: Xcode-12.app not found at $DEVELOPER_DIR" >&2
	exit 1
fi

OUT_DIR="${1:-build/kexts}"
mkdir -p "$OUT_DIR"

KEXTS=(BCM2837InterruptController BCM2837SDHCI SMSC9514Ethernet BCM2837DWC2)

# Compiles every .cpp in each driver directory, not just <dir>/<dir>.cpp:
# BCM2837DWC2 is split across several translation units (controller,
# enumeration, transport) and a one-file-per-directory loop would have
# silently skipped all but one of them.
for kext in "${KEXTS[@]}"; do
	echo "=== compiling $kext ==="
	for src in "iokit/$kext"/*.cpp; do
		[ -e "$src" ] || continue
		obj="$OUT_DIR/$(basename "${src%.cpp}").o"
		"$CLANGXX" -isysroot "$SDK" -arch arm64 -fapple-kext -mkernel \
			$(grep -v '^-xc++$' compile_flags.txt) \
			-c "$src" \
			-o "$obj"
	done
done

echo "=== done, objects in $OUT_DIR/ ==="
ls -la "$OUT_DIR"
