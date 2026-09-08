#!/bin/bash
#
# Compiles real Apple IOUSBFamily (third_party/IOUSBFamily, pinned
# IOUSBFamily-560.4.2) against this project's xnu-7195 kernel headers,
# using the same Xcode-12 kext toolchain as tools/build_kexts.sh.
#
# WHY: DAR-244. This kernel's USB stack is a bespoke DWC2-only
# implementation whose enumeration builds a private DWC2USBDevice table
# rather than publishing IOUSBDevice/IOUSBInterface IOService nubs, so
# no USB driver can ever be matched by a personality -- SMSC9514Ethernet
# is hand-`new`'d inside BCM2837DWC2::start() and self-documents that as
# "BRING-UP SCAFFOLDING, not the final wiring". DAR-243 needs real
# personality matching for AppleUSBCDCECM, which means a real
# IOUSBController/IOUSBDevice/IOUSBInterface/IOUSBPipe layer has to exist
# and be compiled. This script is the first step of that: prove Apple's
# real family source compiles against this kernel, and keep it compiling.
#
# STATUS (2026-09-08): 18 of the family's 19 .cpp files compile clean.
# See "KNOWN SOURCE FIX" below for the 19th. This script produces real
# .o files; it does NOT yet link them into the kernel -- that is the next
# milestone (vendoring into third_party/xnu-7195/iokit/USB/ +
# iokit/conf/files entries, exactly the way iokit/Network/ and
# iokit/Storage/ already vendor IONetworkingFamily and IOStorageFamily).
#
# PREREQUISITES
#   ./setup_third_party.sh          (IOUSBFamily-560.4.2, IOPCIFamily-428.80.2)
#   tools/setup_sdk_shim.sh         (builds <IOKit/usb/>, <IOKit/pci/>)
#   tools/generate_driverkit_headers.sh  (generated/DriverKit/IODMACommand.h etc.)
# All three are cheap and idempotent; this script runs the latter two for
# you rather than failing with a confusing missing-header error.
#
# Not included, deliberately: AppleUSBEHCI/OHCI/UHCI/XHCI (real PCI host
# controllers for hardware this board does not have -- BCM2837DWC2 is the
# host controller here), AppleUSBHub (see the note in the FILES list),
# IOUSBUserClient (userspace IOUSBLib plumbing, no consumer yet), and
# USBProberV2/Examples (userspace tools).

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

FAMILY="third_party/IOUSBFamily"
if [ ! -d "$FAMILY/IOUSBFamily/Classes" ]; then
	echo "error: $FAMILY not cloned - run ./setup_third_party.sh" >&2
	exit 1
fi

# Guard against the IOUSBFamily-630.4.5 empty-source trap (see
# docs/apple-oss-catalog.md's 2026-09-08 addendum). A wrong pin here does
# not fail the build -- it silently compiles nothing at all.
if [ ! -s "$FAMILY/IOUSBFamily/Classes/IOUSBController.cpp" ]; then
	echo "error: $FAMILY/IOUSBFamily/Classes/IOUSBController.cpp is EMPTY." >&2
	echo "       third_party/IOUSBFamily is pinned to a tag that publishes" >&2
	echo "       its core sources as zero-byte files (630.4.5 does this)." >&2
	echo "       Re-pin to IOUSBFamily-560.4.2 and re-clone." >&2
	exit 1
fi

OUT_DIR="${1:-build/iousbfamily}"
SRC_DIR="$OUT_DIR/src"
mkdir -p "$OUT_DIR" "$SRC_DIR"

# ---------------------------------------------------------------------
# Staged source copy + the one known source fix.
#
# KNOWN SOURCE FIX: IOUSBDevice.cpp declares `UInt32 info = NULL;` (in
# SetConfiguration()'s USB-3.0 extra-power path). That was legal in the
# C++98/03 dialect Apple built this family with in 2012, where NULL is
# an integer constant expression; under the -std=gnu++14 this project's
# kernel uses, NULL is nullptr_t and the initialisation is a hard error:
#
#   error: cannot initialize a variable of type 'UInt32' with an rvalue
#          of type 'nullptr_t'
#
# The variable is an out-parameter for GetPortInformation() and is
# overwritten before every read, so zero-initialising it is exactly what
# the original code meant. This is a real (if harmless) bug in Apple's
# published source, not a porting compromise.
#
# It is applied to a staged copy rather than to third_party/ so that
# third_party/IOUSBFamily stays a pristine, unpatched upstream checkout
# (same treatment every other unmodified Apple pin gets). When the family
# is vendored into third_party/xnu-7195/iokit/USB/ for the real kernel
# build, the fix moves there as a real commit on the xnu fork -- the same
# way IOStorageFamily's and msdosfs's local fixes already live in that
# fork rather than in a patch file.
#
# The substitution asserts on its own target: if a future re-pin changes
# that line, this fails loudly instead of silently not applying.
# ---------------------------------------------------------------------
copy_sources() {
	local src dst
	for src in "$FAMILY/IOUSBFamily/Classes"/*.cpp; do
		dst="$SRC_DIR/$(basename "$src")"
		cp "$src" "$dst"
	done
}
copy_sources

# NOTE: LC_ALL=C on both grep and sed is load-bearing, not cosmetic.
# Apple's 2012 sources carry a latin-1 (c) byte in every copyright header,
# which is invalid UTF-8; under this machine's default UTF-8 locale BSD
# sed fails outright with "RE error: illegal byte sequence" and GNU/BSD
# grep silently treats the file as binary and reports nothing -- which
# would make the assertion below pass or fail for entirely the wrong
# reason. (The same trap costs a `grep -a` when searching this tree by
# hand.)
FIXFILE="$SRC_DIR/IOUSBDevice.cpp"
if ! LC_ALL=C grep -q 'UInt32[[:space:]]*info = NULL;' "$FIXFILE"; then
	echo "error: expected 'UInt32 info = NULL;' in IOUSBDevice.cpp and did" >&2
	echo "       not find it. The pin changed; re-check whether the fix is" >&2
	echo "       still needed before deleting this block." >&2
	exit 1
fi
LC_ALL=C sed -i '' \
	's|UInt32\([[:space:]]*\)info = NULL;|UInt32\1info = 0;\t/* was NULL; see tools/build_iousbfamily.sh */|' \
	"$FIXFILE"
if LC_ALL=C grep -q 'UInt32[[:space:]]*info = NULL;' "$FIXFILE"; then
	echo "error: the IOUSBDevice.cpp NULL fix did not apply" >&2
	exit 1
fi

# ---------------------------------------------------------------------
# Include path + flags.
#
# compile_flags.txt is the shared kext recipe (same one build_kexts.sh
# uses); the extra -I entries are IOUSBFamily's own two source-relative
# header roots. `USBTracepoints.h` and `IOUSBFamilyInfoPlist.pch` are
# included with "" from Classes/*.cpp and live in Headers/ and the repo
# root respectively.
# ---------------------------------------------------------------------
CFLAGS=(
	-isysroot "$SDK" -arch arm64 -fapple-kext -mkernel
	-I"$FAMILY/IOUSBFamily/Headers"
	-I"$FAMILY/IOUSBUserClient/Headers"
	-I"$FAMILY"
)

# The family is 2012-vintage Apple source compiled by a 2020 clang: it
# produces thousands of -Wsign-conversion/-Wunused-parameter style
# warnings that are noise, not signal, and would bury a real new warning.
# Errors are never suppressed.
QUIET_WARNINGS=(
	-Wno-sign-conversion
	-Wno-shorten-64-to-32
	-Wno-unused-parameter
	-Wno-unused-variable
	-Wno-deprecated-declarations
	-Wno-inconsistent-missing-override
	-Wno-format
	-Wno-cast-align
)

# Order matters only for readability; every file is independent.
FILES=(
	IOUSBBus.cpp
	IOUSBCommand.cpp
	IOUSBControllerListElement.cpp
	IOUSBWorkLoop.cpp
	IOUSBLog.cpp
	IOUSBNub.cpp
	IOUSBController.cpp
	IOUSBController_Pipes.cpp
	IOUSBController_Errata.cpp
	IOUSBControllerV2.cpp
	IOUSBControllerV3.cpp
	IOUSBControllerUserClient.cpp
	IOUSBDevice.cpp
	IOUSBInterface.cpp
	IOUSBPipe.cpp
	IOUSBPipeV2.cpp
	IOUSBRootHubDevice.cpp
	IOUSBHubDevice.cpp
	IOUSBHubPolicyMaker.cpp
)
# AppleUSBDiagnostics.cpp is deliberately absent: it is an IOService that
# publishes USB debug counters to userspace via a property dictionary,
# has no consumer in this kernel, and pulls in nothing the rest of the
# family needs.

# Make sure the generated/derived header trees this needs actually exist
# and are current. Both are idempotent and fast.
./tools/setup_sdk_shim.sh >/dev/null
if [ ! -f generated/DriverKit/IODMACommand.h ]; then
	./tools/generate_driverkit_headers.sh >/dev/null
fi
if [ ! -f generated/libkern/version.h ]; then
	./tools/generate_libkern_version.sh >/dev/null
fi

failed=()
built=0
for f in "${FILES[@]}"; do
	src="$SRC_DIR/$f"
	if [ ! -f "$src" ]; then
		echo "error: $src missing (wrong pin?)" >&2
		exit 1
	fi
	obj="$OUT_DIR/${f%.cpp}.o"
	echo "=== compiling $f ==="
	if "$CLANGXX" "${CFLAGS[@]}" \
		$(grep -v '^-xc++$' compile_flags.txt) \
		"${QUIET_WARNINGS[@]}" \
		-c "$src" -o "$obj"; then
		built=$((built + 1))
	else
		failed+=("$f")
	fi
done

echo
if [ ${#failed[@]} -ne 0 ]; then
	echo "=== FAILED (${#failed[@]}): ${failed[*]} ===" >&2
	exit 1
fi

echo "=== IOUSBFamily: $built/${#FILES[@]} objects built into $OUT_DIR/ ==="
ls -la "$OUT_DIR"/*.o | awk '{printf "  %-10s %s\n", $5, $9}'
