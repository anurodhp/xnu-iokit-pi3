#!/bin/bash
#
# Compiles real Apple AppleUSBCDCECM (third_party/AppleUSBCDCDriver,
# pinned AppleUSBCDCDriver-4303.2.1) against the real IOUSBFamily layer
# that tools/build_iousbfamily.sh builds, using the same Xcode-12 kext
# toolchain as tools/build_kexts.sh.
#
# WHY: DAR-243. QEMU's `usb-net` device (already the default in
# runvm.sh's invocation, SLIRP-backed) presents a real, standard USB
# CDC-ECM configuration alongside its RNDIS one. This project's only
# Ethernet driver, SMSC9514Ethernet, speaks the real LAN9514's register
# protocol and cannot match it, so QEMU boots have never had a working
# `en0` -- all functional Ethernet testing to date has been on real Pi 3
# hardware. AppleUSBCDCECM is the real Apple driver for exactly that
# device class.
#
# Three drivers get built here, and all three are needed:
#   AppleUSBCDC             the CDC "master" driver. Matches the composite
#                           USB *device*, works out which CDC subclass it
#                           is, and holds the shared state the Control and
#                           Data drivers both look up (getCDCDriver()).
#                           Both ECM drivers reference it -- it is not
#                           optional plumbing that can be skipped.
#   AppleUSBCDCECMControl   matches the ECM Communication interface
#                           (bInterfaceClass=2 / bInterfaceSubClass=6 /
#                           bInterfaceProtocol=0)
#   AppleUSBCDCECMData      matches the ECM Data interface
#                           (bInterfaceClass=10 / bInterfaceSubClass=0)
#                           and is the IOEthernetController that becomes
#                           en0.
# Those match values are confirmed against QEMU's own desc_iface_cdc[] in
# third_party/qemu-zhuowei/hw/usb/dev-network.c, not assumed from the spec.
#
# NOT built: the ACM/DMM/EEM/WCM sibling drivers (other CDC subclasses --
# modems, device management, EEM; no consumer here).
#
# STATUS (2026-09-08): compiles. NOT yet linked into the kernel and NOT
# yet matched against anything -- there is still no IOUSBInterface nub in
# this kernel's IORegistry for a personality to match (DAR-244). Building
# clean is a prerequisite for that work, not a substitute for it. A
# personality wired up before the nubs exist would be a permanently-dead
# entry indistinguishable from working code, which is the exact failure
# class CLAUDE.md's "Build traps: silent no-ops" section warns about.
#
# PREREQUISITES: ./setup_third_party.sh, then tools/build_iousbfamily.sh
# (which also builds the derived header trees this needs).

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

CDC="third_party/AppleUSBCDCDriver"
FAMILY="third_party/IOUSBFamily"
if [ ! -d "$CDC/AppleUSBCDCECM" ]; then
	echo "error: $CDC not cloned - run ./setup_third_party.sh" >&2
	exit 1
fi
if [ ! -s "$FAMILY/IOUSBFamily/Classes/IOUSBController.cpp" ]; then
	echo "error: third_party/IOUSBFamily is missing or pinned to a tag that" >&2
	echo "       publishes empty sources - see tools/build_iousbfamily.sh." >&2
	exit 1
fi

OUT_DIR="${1:-build/appleusbcdc}"
SRC_DIR="$OUT_DIR/src"
mkdir -p "$OUT_DIR" "$SRC_DIR"

# ---------------------------------------------------------------------
# Staged source copy + the one known source fix.
#
# KNOWN SOURCE FIX: AppleUSBCDCECM/DataDriver/Headers/linkup.h assigns
# KMOD_INFO_NAME.name (a `const char[]`) to a `char *` local:
#
#   error: assigning to 'char *' from incompatible type 'const char *'
#
# Legal under the pre-C++11 rules Apple built this with; an error now.
# The pointer is only ever read (walked forward past '.' separators to
# strip the bundle-ID prefix off the kext name for log messages), so
# making the local `const char *` is what the code already means.
#
# Worth flagging beyond the compile error: linkup.h is kext-bundle-only
# machinery. KMOD_INFO_NAME resolves to the kmod_info struct that only
# exists for a driver loaded as a .kext, and this project compiles its
# drivers directly into the kernel with no kext-bundle pipeline at all
# (see CLAUDE.md). Whatever consumes this at link time will need a real
# answer -- most likely defining the driver's kmod_info explicitly, the
# same way any statically-linked-in kext does -- and that is a
# link-milestone problem, deliberately not papered over here.
#
# Applied to a staged copy so third_party/ stays a pristine, unpatched
# upstream checkout, same as tools/build_iousbfamily.sh. LC_ALL=C is
# load-bearing: Apple's latin-1 copyright byte makes BSD sed abort with
# "RE error: illegal byte sequence" and makes grep treat the file as
# binary and silently match nothing.
# ---------------------------------------------------------------------
rm -rf "$SRC_DIR"
mkdir -p "$SRC_DIR"
cp "$CDC/AppleUSBCDC/Classes/AppleUSBCDC.cpp" "$SRC_DIR/"
cp "$CDC/AppleUSBCDCECM/ControlDriver/Classes/AppleUSBCDCECMControl.cpp" "$SRC_DIR/"
cp "$CDC/AppleUSBCDCECM/DataDriver/Classes/AppleUSBCDCECMData.cpp" "$SRC_DIR/"
# linkup.h is included by the Data driver from its own Headers dir; the
# staged copy has to shadow the pristine one on the include path.
mkdir -p "$SRC_DIR/include"
cp "$CDC/AppleUSBCDCECM/DataDriver/Headers/linkup.h" "$SRC_DIR/include/"

FIXFILE="$SRC_DIR/include/linkup.h"
if ! LC_ALL=C grep -q 'char \*kextname' "$FIXFILE"; then
	echo "error: expected 'char *kextname' in linkup.h and did not find it." >&2
	echo "       The pin changed; re-check whether the fix is still needed." >&2
	exit 1
fi
LC_ALL=C sed -i '' \
	's|char \*kextname|const char *kextname|g; s|char \*period|const char *period|g' \
	"$FIXFILE"

CFLAGS=(
	-isysroot "$SDK" -arch arm64 -fapple-kext -mkernel
	-I"$SRC_DIR/include"
	-I"$CDC/Common"
	-I"$CDC/AppleUSBCDC/Headers"
	-I"$CDC/AppleUSBCDCECM/Common"
	-I"$CDC/AppleUSBCDCECM/ControlDriver/Headers"
	-I"$CDC/AppleUSBCDCECM/DataDriver/Headers"
	-I"$FAMILY/IOUSBFamily/Headers"
	-I"$FAMILY/IOUSBUserClient/Headers"
	-I"$FAMILY"
)

QUIET_WARNINGS=(
	-Wno-sign-conversion
	-Wno-shorten-64-to-32
	-Wno-unused-parameter
	-Wno-unused-variable
	-Wno-deprecated-declarations
	-Wno-inconsistent-missing-override
	-Wno-format
	-Wno-cast-align
	-Wno-writable-strings
)

FILES=(
	AppleUSBCDC.cpp
	AppleUSBCDCECMControl.cpp
	AppleUSBCDCECMData.cpp
)

./tools/setup_sdk_shim.sh >/dev/null
if [ ! -f generated/libkern/version.h ]; then
	./tools/generate_libkern_version.sh >/dev/null
fi
if [ ! -f generated/DriverKit/IODMACommand.h ]; then
	./tools/generate_driverkit_headers.sh >/dev/null
fi

failed=()
built=0
for f in "${FILES[@]}"; do
	obj="$OUT_DIR/${f%.cpp}.o"
	echo "=== compiling $f ==="
	if "$CLANGXX" "${CFLAGS[@]}" \
		$(grep -v '^-xc++$' compile_flags.txt) \
		"${QUIET_WARNINGS[@]}" \
		-c "$SRC_DIR/$f" -o "$obj"; then
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

echo "=== AppleUSBCDCECM: $built/${#FILES[@]} objects built into $OUT_DIR/ ==="
ls -la "$OUT_DIR"/*.o | awk '{printf "  %-10s %s\n", $5, $9}'
