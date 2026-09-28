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
# STATUS (2026-09-08): all 19 .cpp files compile clean (the 19th needed
# the IOUSBDevice.cpp fix, now a commit on the anurodhp/IOUSBFamily fork;
# see "Source fixes" below). This script produces real
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
XNU_DK_DIR="third_party/xnu-7195/iokit/DriverKit"
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
mkdir -p "$OUT_DIR"

# Drop objects from previous runs. Without this the success summary at
# the bottom lists stale .o files alongside the current ones, so a source
# removed from FILES still shows up under the "N/N objects built" banner.
# (build/iousbfamily/src/ held staged, sed-fixed source copies before
# DAR-421; it is no longer written or read.)
find "$OUT_DIR" -maxdepth 1 -name '*.o' -delete 2>/dev/null || true

# ---------------------------------------------------------------------
# Source fixes: none here. They are commits on this project's fork,
# anurodhp/IOUSBFamily branch iokit-port (IOUSBFamily-560.4.2 plus one
# commit per fix, citations in the commit messages; DAR-421), which
# setup_third_party.sh clones. This script compiles the checkout's files
# directly.
#
# The one this script used to apply to a staged copy is the fork's
# IOUSBDevice.cpp commit: `UInt32 info = NULL;` in SetConfiguration() is a
# hard error under -std=gnu++14 (NULL is nullptr_t there), and 0 is what
# the code means.
#
# Check for that commit's result so a plain upstream clone fails here with
# a clear message instead of a compile error. LC_ALL=C (and -a) are
# load-bearing: Apple's 2012 sources carry a latin-1 (c) byte in every
# copyright header, which is invalid UTF-8; under a UTF-8 locale grep
# treats the file as binary and reports nothing.
# ---------------------------------------------------------------------
if LC_ALL=C grep -aq 'UInt32[[:space:]]*info = NULL;' "$FAMILY/IOUSBFamily/Classes/IOUSBDevice.cpp"; then
	echo "error: $FAMILY lacks this project's fixes -- it is not the" >&2
	echo "       anurodhp/IOUSBFamily iokit-port fork. Re-clone it:" >&2
	echo "       rm -rf $FAMILY && ./setup_third_party.sh" >&2
	exit 1
fi
SRC_DIR="$FAMILY/IOUSBFamily/Classes"

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
# FILES is the complete contents of IOUSBFamily-560.4.2's Classes/
# directory -- all 19 .cpp files, nothing excluded. (An earlier version
# of this comment claimed AppleUSBDiagnostics.cpp was "deliberately
# absent"; that file exists in 630.4.5 but not in this pin, so the
# rationale described a file that is not there. Removed rather than
# reworded.) The check below keeps the list honest instead of relying on
# a comment: if the pin ever grows or loses a source file, this fails
# rather than silently compiling a subset.
_expected="$(cd "$FAMILY/IOUSBFamily/Classes" && ls *.cpp | LC_ALL=C sort | tr '\n' ' ')"
_listed="$(printf '%s\n' "${FILES[@]}" | LC_ALL=C sort | tr '\n' ' ')"
if [ "$_expected" != "$_listed" ]; then
	echo "error: FILES does not match $FAMILY/IOUSBFamily/Classes/*.cpp" >&2
	echo "  on disk : $_expected" >&2
	echo "  in FILES: $_listed" >&2
	echo "  Update FILES deliberately (and say why, if excluding one)." >&2
	exit 1
fi

# Make sure the generated/derived header trees this needs actually exist
# and are current. Both are idempotent and fast.
#
# Freshness, not just existence. An existence check reuses a stale
# checked-in header forever after an .iig or a pin moves -- the same
# staleness class build_launchd.sh's mtime guard exists to catch, and
# the same one CLAUDE.md calls out as producing "a clean, valid, stale"
# artifact silently.
./tools/setup_sdk_shim.sh >/dev/null

_newer_than() {
	# _newer_than <target> <dir> <glob> -- true if any source is newer
	[ ! -f "$1" ] && return 0
	[ -n "$(find "$2" -name "$3" -newer "$1" -print -quit 2>/dev/null)" ]
}
if _newer_than generated/DriverKit/IODMACommand.h \
		"$XNU_DK_DIR" '*.iig'; then
	echo "=== DriverKit headers stale or missing, regenerating ==="
	./tools/generate_driverkit_headers.sh >/dev/null
fi
if [ ! -f generated/libkern/version.h ] || \
   [ third_party/xnu-7195/config/MasterVersion -nt generated/libkern/version.h ]; then
	echo "=== libkern/version.h stale or missing, regenerating ==="
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

# compat/usb/BCM2837USBKernelGlue.cpp is kernel-only -- it is vendored
# into iokit/USB/ by tools/vendor_usb_into_xnu.sh and compiled by the
# real kernel build, so it is deliberately NOT in FILES above and never
# becomes a .o here. Without this gate nothing in the repo would check it
# short of a full ~20-minute kernel build, which is far too slow a loop
# for a file whose whole job is to satisfy the linker.
#
# Syntax-only, because producing an object with these (Xcode 12) flags
# would be misleading -- the kernel builds it with the live Xcode.
echo "=== syntax-checking compat/usb/BCM2837USBKernelGlue.cpp ==="
if ! "$CLANGXX" "${CFLAGS[@]}" \
	$(grep -v '^-xc++$' compile_flags.txt) \
	"${QUIET_WARNINGS[@]}" \
	-fsyntax-only compat/usb/BCM2837USBKernelGlue.cpp; then
	echo "error: BCM2837USBKernelGlue.cpp does not compile" >&2
	exit 1
fi

echo "=== IOUSBFamily: $built/${#FILES[@]} objects built into $OUT_DIR/ ==="
ls -la "$OUT_DIR"/*.o | awk '{printf "  %-10s %s\n", $5, $9}'
