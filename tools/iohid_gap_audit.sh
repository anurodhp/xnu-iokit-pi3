#!/bin/bash
#
# Compiles the kernel sources of the pinned real Apple IOHIDFamily
# (third_party/IOHIDFamily, IOHIDFamily-1633.81.2, macos-112 manifest)
# plus IOUSBFamily-560.4.2's IOUSBHIDDriver with the REAL flags the
# kernel build uses for iokit C++, and reports, per file, exactly what
# does not resolve. Same shape as tools/iographics_gap_audit.sh (read its
# header for why the flags are extracted from the xnu build rather than
# hand-maintained); this is its DAR-245 sibling.
#
# Source list = the IOHIDFamily kext target's own Sources build phase
# (IOHIDFamily.xcodeproj, target "IOHIDFamily", 90 files), not a guess.
# The two sibling targets IOHIDSystem/IOHIDEventDriver compile nothing of
# their own (Resources-only plist kexts carrying personalities); all
# their classes live in the family binary.
#
#   tools/iohid_gap_audit.sh                   # all sources
#   tools/iohid_gap_audit.sh IOHIDSystem.cpp   # just one
#
# Env knobs: IOHID_AUDIT_KEEP_LOGS=1 keeps per-file logs;
# IOHID_AUDIT_CC_OUT=<path> saves the compiler wrapper for hand re-runs.
#
# Exit status is 0 when it ran, not when the sources are clean -- this is
# an audit, not a gate. Nothing is written into the xnu build tree.
set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

XNU="$REPO_ROOT/third_party/xnu-7195"
HID="$REPO_ROOT/third_party/IOHIDFamily"
USB="$REPO_ROOT/third_party/IOUSBFamily"
OBJDIR="$XNU/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT"
SDK="$REPO_ROOT/build/xnu-latest-build/sdk-7195-ios/iPhoneOS.sdk"
STAGE="$REPO_ROOT/build/iohid/include"

for d in "$HID/IOHIDFamily" "$HID/IOHIDSystem/IOKit/hidsystem" "$USB/IOUSBHIDDriver"; do
	if [ ! -d "$d" ]; then
		echo "error: $d not found - run ./setup_third_party.sh" >&2
		exit 1
	fi
done
if [ ! -f "$OBJDIR/Makefile" ]; then
	echo "error: $OBJDIR/Makefile not found." >&2
	echo "       Run tools/build_xnu_7195.sh once first." >&2
	exit 1
fi

# HIDDriverKit/*.h are generated from the pinned .iig; regenerate when
# missing or older than their definitions.
if [ ! -f generated/HIDDriverKit/IOHIDDevice.h ] || \
   [ -n "$(find "$HID/HIDDriverKit" -name '*.iig' -newer generated/HIDDriverKit/IOHIDDevice.h -print -quit)" ]; then
	./tools/generate_hid_driverkit_headers.sh >/dev/null
fi

# ---------------------------------------------------------------------
# Header layout, reproduced from Apple's own build rather than invented:
#   IOKit/hid       -> IOHIDFamily/   (the target's ShellScript phase:
#                      `ln -sf "$SRCROOT/IOHIDFamily" "$DERIVED_SOURCES_DIR/IOKit/hid"`)
#   IOKit/hidevent  -> IOHIDFamily/   (CopyFiles phases at project.pbxproj
#                      :2610/:2809/:2970 install IOHIDEventDriver.h,
#                      IOHIDEventService.h, IOHIDUtility.h from there)
#   IOKit/hidsystem -> IOHIDSystem/IOKit/hidsystem/
#   HIDDriverKit    -> generated/HIDDriverKit (iig output + IOHIDDeviceTypes.h)
# Symlinks into gitignored build/, rebuilt every run.
# ---------------------------------------------------------------------
#   IOKit/hidsystem/IOHIDUsageTables.h -> IOHIDFamily/IOHIDUsageTables.h
#                      (same fileRef F72B60C0048FC76D00302827 is copied into
#                      hidsystem by the "Copy Kernel HID" phase, project.pbxproj:1068;
#                      IOHIDConsumer/IOHIDKeyboard/IOHIDLibUserClient include it there)
#   IOKit/usb/IOUSBHIDDriver.h -> IOUSBFamily's IOUSBHIDDriver/Headers/
#                      (only that file: the rest of <IOKit/usb/> falls through
#                      to xnu's vendored IOUSBFamily headers)
rm -rf "$STAGE"
mkdir -p "$STAGE/IOKit/hidsystem" "$STAGE/IOKit/usb"
ln -s "$HID/IOHIDFamily" "$STAGE/IOKit/hid"
ln -s "$HID/IOHIDFamily" "$STAGE/IOKit/hidevent"
for h in "$HID/IOHIDSystem/IOKit/hidsystem"/*.h; do ln -s "$h" "$STAGE/IOKit/hidsystem/"; done
ln -s "$HID/IOHIDFamily/IOHIDUsageTables.h" "$STAGE/IOKit/hidsystem/IOHIDUsageTables.h"
ln -s "$USB/IOUSBHIDDriver/Headers/IOUSBHIDDriver.h" "$STAGE/IOKit/usb/IOUSBHIDDriver.h"
ln -s "$USB/IOUSBCompositeDriver/Headers/IOUSBCompositeDriver.h" "$STAGE/IOKit/usb/IOUSBCompositeDriver.h"
ln -s "$REPO_ROOT/generated/HIDDriverKit" "$STAGE/HIDDriverKit"

# Source fixes are commits on this project's forks: anurodhp/IOHIDFamily
# (iokit-port, DAR-356) and anurodhp/IOUSBFamily (iokit-port, DAR-421, for
# IOUSBHIDDriver.cpp). Both checkouts are compiled as they are. Fail here,
# with a clear message, if either is a plain upstream clone rather than
# the fork. grep -a/LC_ALL=C: the sources carry latin-1 bytes.
if ! LC_ALL=C grep -aq 'kHIDPage_AppleVendor[[:space:]]*= 0xFF00' "$HID/IOHIDFamily/AppleHIDUsageTables.h" ||
   [ ! -s "$HID/IOHIDFamily/IOHIDEventServiceFastPathUserClient.cpp" ]; then
	echo "error: $HID is not the anurodhp/IOHIDFamily iokit-port fork." >&2
	echo "       Re-clone it: rm -rf $HID && ./setup_third_party.sh" >&2
	exit 1
fi
if ! LC_ALL=C grep -aq 'strlcpy((char \*)vOutBuf, strBuf, (size_t)strLen);' "$USB/IOUSBHIDDriver/Classes/IOUSBHIDDriver.cpp"; then
	echo "error: $USB is not the anurodhp/IOUSBFamily iokit-port fork." >&2
	echo "       Re-clone it: rm -rf $USB && ./setup_third_party.sh" >&2
	exit 1
fi

# extract_cc <obj> <src> -- prints the normalised compile line for one
# already-built object. Called twice: a C++ probe for the .cpp sources
# and a C probe for the descriptor parser's .c files (compiling C as C++
# reports void*-conversion errors that do not exist in the real build).
extract_cc() {
PROBE_OBJ="$1"
PROBE_SRC="$2"
# -W: treat the source as modified, so `make -n` prints the command even
# when the object is up to date (otherwise it prints nothing).
RAW="$(cd "$OBJDIR" && env -u DEVELOPER_DIR CTFCONVERT=/bin/echo CTFMERGE=/bin/echo \
	make -n -W "$PROBE_SRC" "$PROBE_OBJ" \
		SRCROOT="$XNU" \
		OBJROOT="$XNU/BUILD/obj" \
		DSTROOT="$XNU/BUILD/dst" \
		SYMROOT="$XNU/BUILD/sym" \
		SDKROOT=iphoneos \
		PLATFORM=iPhoneOS \
		PLATFORMPATH=/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform \
		SDKVERSION=26.5 \
		ARCH_CONFIGS=ARM64 \
		MACHINE_CONFIGS=BCM2837 \
		KERNEL_CONFIGS=DEVELOPMENT \
		BUILD_WERROR=0 2>/dev/null \
		| grep -E '/clang(\+\+)? (.* )?-c ' | grep -F "$(basename "$PROBE_SRC")" | head -1)"
# (Selected by `clang -c` + source name, not by `-o <obj>`: xnu's C rule
# has no -o and relies on clang's default output name.)
if [ -z "$RAW" ]; then
	echo "error: could not extract a compile command from $OBJDIR" >&2
	exit 1
fi
# Same normalisation as tools/iographics_gap_audit.sh (see its comments).
python3 - "$RAW" "$PROBE_OBJ" "$PROBE_SRC" "$SDK" <<'PYEOF'
import sys, re, os
raw, obj, src, sdk = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
raw = raw.replace("-o %s " % obj, "")
raw = re.sub(r"\s-M[DP](?=\s)", " ", raw)
raw = re.sub(r"\s-MF\s+\S+", " ", raw)
raw = re.sub(r"\s\S*%s\b" % re.escape(os.path.basename(src)), " ", raw)
raw = re.sub(r"-I\S+/SDKs/iPhoneOS[0-9.]*\.sdk/usr/local/include/kernel",
             "-I%s/usr/local/include/kernel" % sdk, raw)
raw = re.sub(r" -I\S+/EXPORT_HDRS/libsa ", " ", raw)
raw = raw.replace("-c ", "-c -DARM64_BOARD_CONFIG_BCM2837 -mcpu=cortex-a53 ", 1)
print(raw.strip())
PYEOF
}
CC_LINE="$(extract_cc BCM2837Mailbox.cpo "$XNU/iokit/Drivers/BCM2837/BCM2837Mailbox.cpp")"
C_LINE="$(extract_cc IOStringFuncs.o "$XNU/iokit/Kernel/IOStringFuncs.c")"

HID_INC=(
	# IOHIDFamily.xcconfig: GCC_PREPROCESSOR_DEFINITIONS adds
	# APPLE_KEXT_ASSERTIONS=1 (DISPATCH_MACH_SPI/OS_CRASH_* are userspace-
	# only and have no kernel meaning); OTHER_CFLAGS adds -I$(SRCROOT).
	-DAPPLE_KEXT_ASSERTIONS=1
	# Declarations the drop uses and its stripped private headers lack;
	# see that file for the source of every value.
	-include "$REPO_ROOT/compat/iohid/IOHIDFamilyPrivateMissing.h"
	# <IOKit/IOReporter.h>, <IOKit/usb/IOUSBHostFamily.h>: see each file.
	-I"$REPO_ROOT/compat/iohid"
	-I"$STAGE"
	-I"$HID"
	-I"$HID/IOHIDFamily"
	-I"$HID/IOHIDSystem"
	-I"$HID/IOHIDSystem/IOKit/hidsystem"
	-I"$USB/IOUSBHIDDriver/Headers"
	# IOUSBHIDDriver.cpp includes "USBTracepoints.h" (IOUSBFamily/Headers),
	# same header root tools/build_iousbfamily.sh adds.
	-I"$USB/IOUSBFamily/Headers"
	-I"$XNU/iokit"
)

F="$HID/IOHIDFamily"
S="$HID/IOHIDSystem"
P="$HID/IOHIDSystem/IOHIDDescriptorParser"
SOURCES=(
	"$F/AppleEmbeddedHIDEventService.cpp" "$F/AppleEmbeddedKeyboard.cpp"
	"$F/AppleUserHIDDevice.cpp" "$F/AppleUserHIDEventService.cpp"
	"$F/IOHIDActionQueue.cpp" "$F/IOHIDConsumer.cpp" "$F/IOHIDDevice.cpp"
	"$F/IOHIDDeviceElementContainer.cpp" "$F/IOHIDElementContainer.cpp"
	"$F/IOHIDElementPrivate.cpp" "$F/IOHIDEvent.cpp" "$F/IOHIDEventDriver.cpp"
	"$F/IOHIDEventQueue.cpp" "$F/IOHIDEventRepairDriver.cpp"
	"$F/IOHIDEventService.cpp" "$F/IOHIDEventServiceQueue.cpp"
	"$F/IOHIDEventServiceUserClient.cpp" "$F/IOHIDEventSource.cpp"
	"$F/IOHIDEventServiceFastPathUserClient.cpp"
	"$F/IOHIDFamilyPrivate.cpp" "$F/IOHIDInterface.cpp" "$F/IOHIDKeyboard.cpp"
	"$F/IOHIDLibUserClient.cpp" "$F/IOHIDPointing.cpp"
	"$F/IOHIDProviderPropertyMerger.cpp" "$F/IOHIDReportElementQueue.cpp"
	"$F/IOHIDResource.cpp" "$F/IOHIDResourceUserClient.cpp"
	"$F/IOHIDUserDevice.cpp" "$F/IOHIDUtility.cpp"
	"$F/IOHIDEventDummyService/IOHIDEventDummyService.cpp"
	"$S/IOFixed64.cpp" "$S/IOFixedPoint64.cpp" "$S/IOHIDDeviceShim.cpp"
	"$S/IOHIDIO.cpp" "$S/IOHIDKeyboardDevice.cpp"
	"$S/IOHIDKeyboardEventDevice.cpp" "$S/IOHIDPointingDevice.cpp"
	"$S/IOHIDPointingEventDevice.cpp" "$S/IOHIDPrivate.cpp" "$S/IOHIDSystem.cpp"
	"$S/IOHIDSystemCursorHelper.cpp" "$S/IOHIDUserClient.cpp"
	"$S/IOHIDWorkLoop.cpp" "$S/IOHIDevice.cpp" "$S/IOHIKeyboard.cpp"
	"$S/IOHIKeyboardMapper.cpp" "$S/IOHIPointing.cpp" "$S/IOHITablet.cpp"
	"$S/IOHITabletPointer.cpp"
	"$REPO_ROOT/generated/HIDDriverKit/impl/IOHIDDevice.iig.cpp"
	"$REPO_ROOT/generated/HIDDriverKit/impl/IOHIDInterface.iig.cpp"
	"$REPO_ROOT/generated/HIDDriverKit/impl/IOHIDEventService.iig.cpp"
	# anurodhp/IOUSBFamily fork (its case (UInt32)-1 and strLen fixes)
	"$USB/IOUSBHIDDriver/Classes/IOUSBHIDDriver.cpp"
	# IOUSBFamily-560.4.2's generic device driver: configures a class-0
	# (composite) device so its IOUSBInterface nubs exist for IOUSBHIDDriver.
	"$USB/IOUSBCompositeDriver/Classes/IOUSBCompositeDriver.cpp"
)
# The descriptor parser is C (compiled with the kernel's C line below),
# and so is Cosmo_USB2ADB.c: the USB<->ADB keycode tables
# (hid_usb_2_adb_keymap etc.) that IOHIDKeyboard/IOHIDEventDriver link against.
SOURCES+=("$F/Cosmo_USB2ADB.c")
for c in "$P"/*.c; do SOURCES+=("$c"); done

if [ $# -gt 0 ]; then
	filtered=()
	for s in "${SOURCES[@]}"; do
		for w in "$@"; do
			[ "$(basename "$s")" = "$w" ] && filtered+=("$s")
		done
	done
	SOURCES=("${filtered[@]}")
	if [ ${#SOURCES[@]} -eq 0 ]; then
		echo "error: no known IOHIDFamily source matched: $*" >&2
		exit 1
	fi
fi

for s in "${SOURCES[@]}"; do
	[ -f "$s" ] || { echo "error: $s missing (wrong pin?)" >&2; exit 1; }
done

LOGDIR="$(mktemp -d /tmp/iohid_gap_audit.XXXXXX)"
if [ -n "${IOHID_AUDIT_KEEP_LOGS:-}" ]; then
	trap 'echo "logs kept in $LOGDIR"' EXIT
else
	trap 'rm -rf "$LOGDIR"' EXIT
fi
printf '#!/bin/bash\ncd %q\n%s "$@"\n' "$OBJDIR" "$CC_LINE" > "$LOGDIR/cc.sh"
printf '#!/bin/bash\ncd %q\n%s "$@"\n' "$OBJDIR" "$C_LINE" > "$LOGDIR/c.sh"
[ -n "${IOHID_AUDIT_CC_OUT:-}" ] && cp "$LOGDIR/cc.sh" "$IOHID_AUDIT_CC_OUT"

echo "=== IOHIDFamily gap audit (real kernel iokit C++ flags) ==="
echo "    source: $HID  ($(cd "$HID" && git describe --tags 2>/dev/null || echo 'unknown tag'))"
echo

total=0
clean=0
for src in "${SOURCES[@]}"; do
	name="$(basename "$src")"
	log="$LOGDIR/$name.log"
	case "$src" in
		*.c) drv="$LOGDIR/c.sh" ;;
		*)   drv="$LOGDIR/cc.sh" ;;
	esac
	bash "$drv" "${HID_INC[@]}" -o /dev/null "$src" > "$log" 2>&1 || true
	n=$(grep -c "error:" "$log" || true)
	total=$((total + n))
	if [ "$n" -eq 0 ]; then
		clean=$((clean + 1))
		printf '%-40s %s\n' "$name" "compiles clean"
	else
		printf '%-40s %s\n' "$name" "$n errors"
		grep "error:" "$log" \
			| sed 's/.*error: //' \
			| sort | uniq -c | sort -rn | head -12 \
			| sed 's/^/        /'
	fi
done

echo
echo "=== $clean/${#SOURCES[@]} sources compile clean; $total errors total ==="
