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
# Wipe both the staged sources and any previously-built objects. Without
# the OUT_DIR wipe the success summary at the bottom lists stale .o files
# from earlier runs next to the current ones, so a source dropped from
# FILES still appears under the "N/N objects built" banner.
rm -rf "$SRC_DIR"
find "$OUT_DIR" -maxdepth 1 -name '*.o' -delete 2>/dev/null || true
mkdir -p "$SRC_DIR"
cp "$CDC/AppleUSBCDC/Classes/AppleUSBCDC.cpp" "$SRC_DIR/"
cp "$CDC/AppleUSBCDCECM/ControlDriver/Classes/AppleUSBCDCECMControl.cpp" "$SRC_DIR/"
cp "$CDC/AppleUSBCDCECM/DataDriver/Classes/AppleUSBCDCECMData.cpp" "$SRC_DIR/"
cp "$CDC/Common/MsgTrace.c" "$SRC_DIR/"
# linkup.h is included by the Data driver from its own Headers dir; the
# staged copy has to shadow the pristine one on the include path.
mkdir -p "$SRC_DIR/include"
cp "$CDC/AppleUSBCDCECM/DataDriver/Headers/linkup.h" "$SRC_DIR/include/"

# The ONLY thing that needs changing is the `char *period` local: it is
# assigned from strchr(), which returns const char* when its argument is
# const char*. `kextname` is already `const char *` in the pristine
# header (parameter list, line ~40) and must NOT be rewritten -- doing so
# produced `const const char *kextname` and a duplicate-'const' warning.
# Assert on the real target, and re-check afterwards that it applied,
# rather than on something that is true either way.
FIXFILE="$SRC_DIR/include/linkup.h"
if ! LC_ALL=C grep -q 'char \*period;' "$FIXFILE"; then
	echo "error: expected 'char *period;' in linkup.h and did not find it." >&2
	echo "       The pin changed; re-check whether the fix is still needed" >&2
	echo "       before deleting this block." >&2
	exit 1
fi
LC_ALL=C sed -i '' 's|	char \*period;|	const char *period;|' "$FIXFILE"
if LC_ALL=C grep -q '	char \*period;' "$FIXFILE"; then
	echo "error: the linkup.h const fix did not apply" >&2
	exit 1
fi
if LC_ALL=C grep -q 'const const' "$FIXFILE"; then
	echo "error: linkup.h fix produced a duplicate 'const'" >&2
	exit 1
fi

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

# MsgTrace.c is built separately because it is C, not C++, and that is
# load-bearing rather than incidental. AppleUSBCDCCommon.h:507 declares
# cdc_LogToMessageTracer() inside an `extern "C"` block, so the C++
# callers emit an undefined `_cdc_LogToMessageTracer` with no mangling;
# only a C compile of MsgTrace.c defines that exact symbol.
#
# It cannot go through compile_flags.txt: that recipe is C++ (-std=gnu++14)
# and force-includes compat/decls.h, which contains a real `extern "C" {`
# block and does not parse as C at all. So the flags below are the same
# -D/-I set spelled out for C, plus -D_STRINGS_H_ by hand -- that is the
# one thing compat/decls.h does that a C compile still needs. Without it
# the SDK's usr/include/strings.h gets pulled in (via a quoted include
# from string.h that no -I list can intercept) and its userspace
# ffs/ffsll/fls/flsll signatures collide head-on with the kernel's
# unsigned ones in bsd/libkern/libkern.h. Same reasoning as decls.h's own
# header comment; see it for why an -Icompat override cannot work.
#
# This file was missed on the first pass (2026-09-08) and the omission
# went unnoticed because it does not break the compile -- it breaks the
# eventual link, which nothing runs yet. Caught by an adversarial review
# reading `nm -u` rather than trusting the build's own exit status, which
# is exactly the check the commit message had claimed to have done.
C_FILES=(
	MsgTrace.c
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

CLANGC="$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang"
for f in "${C_FILES[@]}"; do
	obj="$OUT_DIR/${f%.c}.o"
	echo "=== compiling $f (as C) ==="
	if "$CLANGC" -isysroot "$SDK" -arch arm64 -mkernel \
		-x c -std=gnu11 \
		-D_STRINGS_H_ \
		-DKERNEL -DKERNEL_PRIVATE -DXNU_KERNEL_PRIVATE -DPRIVATE \
		-DAPPLE_KEXT_LEGACY_ABI=0 -DARM64_BOARD_CONFIG_BCM2837 \
		-D__PRIVATE_SPI__ \
		-Icompat -Ithird_party/_sdk_shim -Igenerated \
		-Ithird_party/xnu-7195/pexpert -Ithird_party/xnu-7195/iokit \
		-Ithird_party/xnu-7195/libkern -Ithird_party/xnu-7195/osfmk \
		-Ithird_party/xnu-7195/bsd \
		-Ithird_party/xnu-7195/EXTERNAL_HEADERS -Ithird_party/xnu-7195 \
		-I"$CDC/Common" \
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

# Hard gate, not a nicety: the whole point of building these objects is
# that they eventually link into the kernel, and the one class of bug
# that a successful compile cannot catch is a missing translation unit.
# Everything the CDC drivers reference must resolve either inside this
# object set or against IOKit/IONetworkingFamily/IOUSBFamily, which the
# kernel will provide. Anything else is a real gap and must be listed
# here deliberately.
#
# _kmod_info is one known, deliberate exception: linkup.h's
# KMOD_INFO_NAME refers to the kmod_info struct that only exists for a
# driver loaded as a .kext bundle, and this project compiles drivers
# directly into the kernel with no kext-bundle pipeline at all. It needs
# a real answer at the vendoring milestone (most likely an explicit
# kmod_info definition, as any statically-linked-in kext has), not a stub.
#
# Method: undefined-across-the-set MINUS defined-across-the-set, so a
# symbol one object needs and another object provides does not register.
# That subtraction is the part that matters -- it is exactly what would
# have caught the missing MsgTrace.c the first time round.
#
# C++-mangled symbols (__Z...) are dropped wholesale: those are IOKit,
# IONetworkingFamily and IOUSBFamily methods, all of which the kernel
# will provide. What is left is plain C.
#
# DAR-294: this gate previously never actually failed (see below), so
# this list was never exercised for real - a real first run surfaced 22
# more plain-C symbols, every one confirmed (by grepping this project's
# own real xnu-7195 checkout, not assumed) as a real Libkern/BSD/IOKit
# kernel export this driver set genuinely expects the kernel to
# provide at final link, the same "the kernel will provide it" case the
# comment above already describes for C++ symbols, just not yet
# extended to cover plain C ones:
#   IOFree/IOLog/IOMalloc_internal/IOSleep/kprintf - osfmk/iokit basics
#   KHEAP_DEFAULT - osfmk/kern/kalloc.h real kalloc heap descriptor
#   OSObject_operator_delete$XNU_INTERNAL/OSObject_operator_new$XNU_INTERNAL
#     - libkern/c++/OSObject.h's real placement new/delete, exported via
#     config/Libkern.exports
#   PAGE_SHIFT_CONST - osfmk/mach/arm/vm_param.h, already referenced by
#     this project's own already-vendored SMSC9514Ethernet.cpp
#   bcopy/bzero/snprintf/strchr/__strlcat_chk - real BSD/Libc primitives
#     the kernel itself links
#   gIOEthernetWakeOnLANFilterGroup/gIONetworkFilterGroup - real IOKit
#     globals declared in iokit/IOKit/network/IOEthernetController.h and
#     IONetworkController.h, defined in Network/IOEthernetInterface.cpp
#   kOSBooleanTrue/kOSBooleanFalse - real Libkern OSBoolean singletons
#   mbuf_copyback/mbuf_data/mbuf_len/mbuf_next - real BSD mbuf KPI
KNOWN_UNRESOLVED='^_(kmod_info|IOFree|IOLog|IOMalloc_internal|IOSleep|KHEAP_DEFAULT|OSObject_operator_delete\$XNU_INTERNAL|OSObject_operator_new\$XNU_INTERNAL|PAGE_SHIFT_CONST|__strlcat_chk|bcopy|bzero|gIOEthernetWakeOnLANFilterGroup|gIONetworkFilterGroup|kOSBooleanFalse|kOSBooleanTrue|kprintf|mbuf_copyback|mbuf_data|mbuf_len|mbuf_next|snprintf|strchr)$'
# NOTE on the nm invocation, learned the hard way while writing this:
# plain `nm` CANNOT be used here. This script exports DEVELOPER_DIR
# pointing at Xcode 12, and /usr/bin/nm is an xcrun shim that resolves
# through `xcodebuild -find nm` -- which crashes on this host, the exact
# breakage tools/build_kexts.sh's header documents ("xcodebuild/xcrun
# machinery cannot run at all on this host... crashes trying to load a
# plugin that links against CoreChart.framework"). With stderr discarded
# that produces an EMPTY symbol list, which reads exactly like "nothing
# unresolved" and turns this whole gate into a no-op that always passes.
# The same commands work perfectly when run by hand, where DEVELOPER_DIR
# is unset -- so it fails only inside the script, which is the worst
# possible shape for a check like this.
#
# Invoke the toolchain binary directly, exactly as this script already
# does for clang/clang++, keep stderr visible, and assert the result is
# non-empty.
NMBIN="$DEVELOPER_DIR/Toolchains/XcodeDefault.xctoolchain/usr/bin/nm"
_nm_tmp="$(mktemp -d)"
"$NMBIN" -g "$OUT_DIR"/*.o \
	| LC_ALL=C awk 'NF==3 && $2 != "U" {print $3}' \
	| LC_ALL=C sort -u > "$_nm_tmp/defined"
"$NMBIN" -g "$OUT_DIR"/*.o \
	| LC_ALL=C awk 'NF==2 && $1 == "U" {print $2}' \
	| LC_ALL=C sort -u > "$_nm_tmp/undefined"
if [ ! -s "$_nm_tmp/undefined" ]; then
	echo "error: nm produced no undefined symbols at all for $OUT_DIR/*.o," >&2
	echo "       which cannot be right -- the symbol gate is not working." >&2
	rm -rf "$_nm_tmp"
	exit 1
fi
# `|| true` is required, not defensive: under `set -e` a grep that matches
# nothing (the good case here) exits 1 and would abort the script.
unexpected=$(LC_ALL=C comm -23 "$_nm_tmp/undefined" "$_nm_tmp/defined" \
	| LC_ALL=C grep -vE '^__?Z' \
	| LC_ALL=C grep -vE '^___stack_chk' \
	| LC_ALL=C grep -vE "$KNOWN_UNRESOLVED" || true)
rm -rf "$_nm_tmp"
# DAR-294: this is the hard gate the comment block above describes -
# "must be listed here deliberately" meant fail the build, not print a
# note and exit 0. An unexpected plain-C symbol here is exactly the
# missing-translation-unit class of bug this whole check exists to
# catch (same failure class as the "check that always passes" already
# fixed for the sibling nm-invocation gate above, DAR-243's own
# adversarial review).
if [ -n "$unexpected" ]; then
	echo "error: plain-C symbols this object set does not define, and that are" >&2
	echo "       not covered by KNOWN_UNRESOLVED above - a real, undocumented gap" >&2
	echo "       (missing translation unit, or a symbol that needs adding to" >&2
	echo "       KNOWN_UNRESOLVED with a real citation for why it's expected):" >&2
	echo "$unexpected" | sed 's/^/  /' >&2
	exit 1
fi

echo "=== AppleUSBCDCECM: $built/$(( ${#FILES[@]} + ${#C_FILES[@]} )) objects built into $OUT_DIR/ ==="
ls -la "$OUT_DIR"/*.o | awk '{printf "  %-10s %s\n", $5, $9}'
