#!/bin/bash
#
# Regenerates generated/DriverKit/*.h from
# third_party/xnu-7195/iokit/DriverKit/*.iig using Xcode 12's `iig`.
#
# Why these are needed for a standalone kext compile at all: xnu-7195's
# own `iokit/IOKit/*.h` headers `#include <DriverKit/IOService.h>`,
# `<DriverKit/IODMACommand.h>` and friends. Inside a real kernel build
# those are produced on the fly by `iokit/DriverKit/Makefile` (the
# `$(IIG) --def ... --header ...` rule); `tools/build_kexts.sh` compiles
# outside that build, and Xcode 12's iPhoneOS SDK has no
# DriverKit.framework, so the same headers have to exist somewhere on the
# include path. `compile_flags.txt` puts `-Igenerated` there.
#
# Until 2026-09-08 the tree under generated/DriverKit was produced by
# hand-run iig invocations that existed only in a shell history, with
# `tools/build_kexts.sh`'s header comment saying "regenerate them with
# mig/iig (see git history of this file's commit...)" instead of shipping
# the command. This script is that command. It was written while adding
# DriverKit/IODMACommand.h, which the by-hand set was missing -- and
# which nothing needed until IOUSBFamily's IOUSBCommand.h pulled in
# <IOKit/IODMACommand.h> for the DAR-243/DAR-244 USB work.
#
# The flags mirror iokit/DriverKit/Makefile's OTHER_IIG_CFLAGS
# (IIG_INCFLAGS + -x c++ -std=gnu++1z -D__IIG=1 -DDRIVERKIT_PRIVATE=1),
# not a guess.
#
# Idempotent. Regenerating produces byte-identical output for headers
# that were already correct, so `git status` after a run is a real check
# that the checked-in copies match their .iig sources.

set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

export DEVELOPER_DIR=/Applications/Xcode-12.app/Contents/Developer
IIG="$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin/iig"
SDK="$DEVELOPER_DIR/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk"

if [ ! -x "$IIG" ]; then
	echo "error: iig not found at $IIG (Xcode-12.app required)" >&2
	exit 1
fi

XNU="third_party/xnu-7195"
SRC="$XNU/iokit/DriverKit"
OUT="generated/DriverKit"

if [ ! -d "$SRC" ]; then
	echo "error: $SRC not found - run ./setup_third_party.sh" >&2
	exit 1
fi

mkdir -p "$OUT"

# iig also emits a .iig.cpp implementation file per definition. Those are
# the userspace/driverkit-side glue and are NOT wanted here (nothing in
# this project compiles them), so they go to a scratch directory and are
# discarded -- iig requires --impl, it is not optional.
IMPL_SCRATCH="$(mktemp -d)"
trap 'rm -rf "$IMPL_SCRATCH"' EXIT

generated=0
for def in "$SRC"/*.iig; do
	[ -e "$def" ] || continue
	base="$(basename "$def" .iig)"
	"$IIG" --def "$def" \
		--header "$OUT/$base.h" \
		--impl "$IMPL_SCRATCH/$base.iig.cpp" \
		--framework-name DriverKit \
		-- \
		-I"$XNU/iokit" -I"$XNU/osfmk" -I"$XNU/bsd" \
		-isysroot "$SDK" -x c++ -std=gnu++1z -D__IIG=1 -DDRIVERKIT_PRIVATE=1
	generated=$((generated + 1))
done

echo "=== generated $generated DriverKit headers into $OUT/ ==="
ls "$OUT"
