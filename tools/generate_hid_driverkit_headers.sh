#!/bin/bash
#
# Regenerates generated/HIDDriverKit/ from the pinned real IOHIDFamily's
# three .iig definitions (third_party/IOHIDFamily/HIDDriverKit/*.iig)
# using Xcode 12's `iig`. Same shape and same reasons as
# tools/generate_driverkit_headers.sh -- read that header first.
#
# WHY (DAR-245): IOHIDFamily-1633's kernel headers include the generated
# ones unconditionally -- IOHIDDevice.h:37 `#include <HIDDriverKit/
# IOHIDDevice.h>`, IOHIDInterface.h:30, IOHIDEventService.h:42 -- and the
# kernel classes carry the iig-generated dispatch members (the
# IOHIDDevice_Methods / _KernelMethods macros). Apple's kext build runs
# iig as part of the target; this is that step, reproducibly.
#
# Unlike generate_driverkit_headers.sh this KEEPS the .iig.cpp impl
# files (generated/HIDDriverKit/impl/). For a KERNEL-annotated class the
# impl is not userspace glue: it defines the kernel-side _Dispatch()
# methods and the OSMetaClass-backed invoke stubs the headers declare,
# and the kext links them. Leaving them out would be a link gap, not a
# size saving.
#
# THE ONE NON-OBVIOUS FLAG: -DPLATFORM_DriverKit=1. iig itself defines
# KERNEL as an annotation (`class KERNEL IOHIDDevice`), so any header on
# the iig pass that does `#if KERNEL` fails with "function-like macro
# '__attribute__' is not defined". IOHIDDeviceTypes.h pulls in
# <DriverKit/IOTypes.h>, and xnu's raw iokit/DriverKit/IOTypes.h:36 does
# exactly that `#if KERNEL` -- unless PLATFORM_DriverKit is set, in which
# case it takes its DriverKit branch (IOTypes.h:275-299: plain stdint
# typedefs). That branch is the view Apple's own HIDDriverKit iig pass
# sees, since Apple runs it against the installed DriverKit SDK headers,
# not xnu's source tree. It only affects how iig PARSES the interface;
# the generated header is then compiled in the ordinary kernel context.
#
# HIDDriverKit/IOHIDDeviceTypes.h is copied (not generated): Apple's
# "Copy DriverKit Headers" phase installs it into
# Kernel.framework/Headers/HIDDriverKit (IOHIDFamily.xcodeproj
# project.pbxproj:2462-2471), next to the generated headers.
#
# Idempotent; `git status generated/HIDDriverKit` after a run is the
# check that the checked-in copies match the pinned .iig sources.

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
HID="third_party/IOHIDFamily"
SRC="$HID/HIDDriverKit"
OUT="generated/HIDDriverKit"

if [ ! -d "$SRC" ]; then
	echo "error: $SRC not found - run ./setup_third_party.sh" >&2
	exit 1
fi

rm -rf "$OUT"
mkdir -p "$OUT/impl"
cp "$HID/IOHIDFamily/IOHIDDeviceTypes.h" "$OUT/IOHIDDeviceTypes.h"

generated=0
for def in "$SRC"/*.iig; do
	base="$(basename "$def" .iig)"
	"$IIG" --def "$def" \
		--header "$OUT/$base.h" \
		--impl "$OUT/impl/$base.iig.cpp" \
		--framework-name HIDDriverKit \
		-- \
		-Igenerated \
		-I"$XNU/iokit" -I"$XNU/osfmk" -I"$XNU/bsd" -I"$XNU/libkern" \
		-isysroot "$SDK" -x c++ -std=gnu++1z -D__IIG=1 \
		-DDRIVERKIT_PRIVATE=1 -DPLATFORM_DriverKit=1
	if [ ! -s "$OUT/$base.h" ] || [ ! -s "$OUT/impl/$base.iig.cpp" ]; then
		# iig leaves zero-byte outputs behind on failure; do not let
		# one sit on the include path looking like a real header.
		echo "error: iig produced empty output for $def" >&2
		rm -rf "$OUT"
		exit 1
	fi
	generated=$((generated + 1))
done

echo "=== generated $generated HIDDriverKit headers into $OUT/ ==="
ls -R "$OUT"
