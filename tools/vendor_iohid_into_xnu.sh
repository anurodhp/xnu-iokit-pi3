#!/bin/bash
#
# Vendors the kernel half of real Apple IOHIDFamily (third_party/IOHIDFamily,
# IOHIDFamily-1633.81.2, macos-112 manifest pin) plus IOUSBFamily-560.4.2's
# IOUSBHIDDriver into the kernel source tree, so USB HID keyboards reach
# the console. DAR-245. Same shape as tools/vendor_iographics_into_xnu.sh
# (read that header first: the copy-not-include-path rationale, the
# divergence guard, the .cpo per-object hook and the _INCFLAGS trap are
# identical):
#
#   iokit/HID/IOHIDFamily/        <- IOHIDFamily/*.cpp *.h (+ IOHIDEventDummyService/)
#   iokit/HID/IOHIDSystem/        <- IOHIDSystem/*.cpp *.h
#   iokit/HID/IOHIDSystem/IOHIDDescriptorParser/  <- the C report parser
#   iokit/HID/HIDDriverKit/       <- generated/HIDDriverKit/impl/*.iig.cpp
#   iokit/HID/IOUSBHIDDriver/     <- IOUSBHIDDriver.cpp (patched copy)
#                                    + IOUSBCompositeDriver.cpp (unmodified; it
#                                    SetConfiguration()s a class-0 device so the
#                                    IOUSBInterface nubs IOUSBHIDDriver matches
#                                    exist -- nothing else does that for a
#                                    generic keyboard)
#   iokit/HID/compat/             <- compat/iohid/ (force-included + redirects)
#   iokit/HIDDriverKit/           <- generated/HIDDriverKit/*.h  (<HIDDriverKit/...>)
#   iokit/IOKit/hid/              <- IOHIDFamily/*.h   (Apple's IOKit/hid symlink phase)
#   iokit/IOKit/hidevent/         <- IOHIDEventDriver.h, IOHIDEventService.h, IOHIDUtility.h
#   iokit/IOKit/hidsystem/        <- IOHIDSystem/IOKit/hidsystem/*.h + IOHIDUsageTables.h
#   iokit/IOKit/usb/IOUSBHIDDriver.h, IOUSBCompositeDriver.h
#
# That header layout is the one tools/iohid_gap_audit.sh stages, with the
# same evidence for each mapping, so the audit remains the fast pre-check
# for this build.
#
# SOURCE LIST = the audit's SOURCES (the IOHIDFamily kext target's own
# Sources phase + the 3 iig impls + IOUSBHIDDriver), minus the two
# DriverKit-dext bridges below.
#
# NOT BUILT, deliberately:
#   AppleUserHIDDevice.cpp, AppleUserHIDEventService.cpp -- kernel halves of
#   DriverKit HID *dexts* (IOUserServer-hosted drivers). This project has no
#   dext loading, so no personality could ever instantiate them. They compile
#   (the audit checks), and adding them back is one line if dexts ever exist.
#
# SOURCE FIXES: IOHIDFamily's are commits on the fork anurodhp/IOHIDFamily
# (branch iokit-port, DAR-356), which setup_third_party.sh clones.
# IOUSBHIDDriver.cpp's are applied to a staged copy by
# tools/patch_iohid_gaps.sh, which runs first and also checks that the
# IOHIDFamily checkout is the fork. The copies under third_party/xnu-7195
# are GENERATED and are overwritten.
#
# Run directly, or let tools/build_xnu_7195.sh call it. Idempotent.

set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

XNU="third_party/xnu-7195"
HID="third_party/IOHIDFamily"
USB="third_party/IOUSBFamily"
CONF="$XNU/iokit/conf/files"
MKT="$XNU/iokit/conf/Makefile.template"

for f in "$HID/IOHIDFamily/IOHIDDevice.cpp" "$HID/IOHIDSystem/IOHIDSystem.cpp" \
	 "$USB/IOUSBHIDDriver/Headers/IOUSBHIDDriver.h"; do
	if [ ! -f "$f" ]; then
		echo "error: $f not found - run ./setup_third_party.sh" >&2
		exit 1
	fi
done

# Check the IOHIDFamily checkout is the fork and stage the patched
# IOUSBHIDDriver.cpp first,
# and make sure the generated HIDDriverKit headers match the pinned .iig.
"$REPO_ROOT/tools/patch_iohid_gaps.sh" > /dev/null
if [ ! -f generated/HIDDriverKit/IOHIDDevice.h ] || \
   [ -n "$(find "$HID/HIDDriverKit" -name '*.iig' -newer generated/HIDDriverKit/IOHIDDevice.h -print -quit)" ]; then
	"$REPO_ROOT/tools/generate_hid_driverkit_headers.sh" >/dev/null
fi

D="$XNU/iokit/HID"
D_FAM="$D/IOHIDFamily"
D_DUMMY="$D/IOHIDFamily/IOHIDEventDummyService"
D_SYS="$D/IOHIDSystem"
D_PARSER="$D/IOHIDSystem/IOHIDDescriptorParser"
D_DKIMPL="$D/HIDDriverKit"
D_USBHID="$D/IOUSBHIDDriver"
D_COMPAT="$D/compat"
H_DK="$XNU/iokit/HIDDriverKit"
H_HID="$XNU/iokit/IOKit/hid"
H_HIDEV="$XNU/iokit/IOKit/hidevent"
H_HIDSYS="$XNU/iokit/IOKit/hidsystem"
H_USB="$XNU/iokit/IOKit/usb"

mkdir -p "$D_FAM" "$D_DUMMY" "$D_SYS" "$D_PARSER" "$D_DKIMPL" "$D_USBHID" \
	 "$D_COMPAT/IOKit/usb" "$H_DK" "$H_HID" "$H_HIDEV" "$H_HIDSYS" "$H_USB"

diverged=()
check_divergence() {
	local dst="$1" src="$2"
	[ -e "$dst" ] || return 0
	cmp -s "$src" "$dst" && return 0
	local rel="${dst#$XNU/}"
	if [ -n "$(git -C "$XNU" status --porcelain -- "$rel" 2>/dev/null)" ]; then
		diverged+=("$rel")
		return 1
	fi
}
copy_into() {
	local dst="$1"; shift
	local f
	for f in "$@"; do
		if check_divergence "$dst/$(basename "$f")" "$f"; then
			cp "$f" "$dst/$(basename "$f")"
		fi
	done
}

FAM_SRCS="AppleEmbeddedHIDEventService AppleEmbeddedKeyboard
	IOHIDActionQueue IOHIDConsumer IOHIDDevice IOHIDDeviceElementContainer
	IOHIDElementContainer IOHIDElementPrivate IOHIDEvent IOHIDEventDriver
	IOHIDEventQueue IOHIDEventRepairDriver IOHIDEventService
	IOHIDEventServiceQueue IOHIDEventServiceUserClient
	IOHIDEventServiceFastPathUserClient IOHIDEventSource
	IOHIDFamilyPrivate IOHIDInterface IOHIDKeyboard IOHIDLibUserClient
	IOHIDPointing IOHIDProviderPropertyMerger IOHIDReportElementQueue
	IOHIDResource IOHIDResourceUserClient IOHIDUserDevice IOHIDUtility"
SYS_SRCS="IOFixed64 IOFixedPoint64 IOHIDDeviceShim IOHIDIO
	IOHIDKeyboardDevice IOHIDKeyboardEventDevice IOHIDPointingDevice
	IOHIDPointingEventDevice IOHIDPrivate IOHIDSystem IOHIDSystemCursorHelper
	IOHIDUserClient IOHIDWorkLoop IOHIDevice IOHIKeyboard IOHIKeyboardMapper
	IOHIPointing IOHITablet IOHITabletPointer"
DK_IMPLS="IOHIDDevice IOHIDInterface IOHIDEventService"

for s in $FAM_SRCS; do copy_into "$D_FAM" "$HID/IOHIDFamily/$s.cpp"; done
copy_into "$D_FAM" "$HID/IOHIDFamily"/*.h
# C: the USB<->ADB keycode tables IOHIDKeyboard and IOHIDEventDriver use.
copy_into "$D_FAM" "$HID/IOHIDFamily/Cosmo_USB2ADB.c"
copy_into "$D_DUMMY" "$HID/IOHIDFamily/IOHIDEventDummyService/IOHIDEventDummyService.cpp" \
	"$HID/IOHIDFamily/IOHIDEventDummyService/IOHIDEventDummyService.h"
for s in $SYS_SRCS; do copy_into "$D_SYS" "$HID/IOHIDSystem/$s.cpp"; done
copy_into "$D_SYS" "$HID/IOHIDSystem"/*.h
copy_into "$D_PARSER" "$HID/IOHIDSystem/IOHIDDescriptorParser"/*.c \
	"$HID/IOHIDSystem/IOHIDDescriptorParser"/*.h
for s in $DK_IMPLS; do copy_into "$D_DKIMPL" "generated/HIDDriverKit/impl/$s.iig.cpp"; done
copy_into "$D_USBHID" build/iohid/src/IOUSBHIDDriver.cpp \
	"$USB/IOUSBCompositeDriver/Classes/IOUSBCompositeDriver.cpp"
copy_into "$D_COMPAT" compat/iohid/IOHIDFamilyPrivateMissing.h compat/iohid/math.h
mkdir -p "$D_COMPAT/IOKit"
copy_into "$D_COMPAT/IOKit" compat/iohid/IOKit/IOReporter.h
copy_into "$D_COMPAT/IOKit/usb" compat/iohid/IOKit/usb/IOUSBHostFamily.h

# Public header layout (see header comment).
copy_into "$H_DK" generated/HIDDriverKit/*.h
copy_into "$H_HID" "$HID/IOHIDFamily"/*.h
copy_into "$H_HIDEV" "$HID/IOHIDFamily/IOHIDEventDriver.h" \
	"$HID/IOHIDFamily/IOHIDEventService.h" "$HID/IOHIDFamily/IOHIDUtility.h"
copy_into "$H_HIDSYS" "$HID/IOHIDSystem/IOKit/hidsystem"/*.h \
	"$HID/IOHIDFamily/IOHIDUsageTables.h"
copy_into "$H_USB" "$USB/IOUSBHIDDriver/Headers/IOUSBHIDDriver.h" \
	"$USB/IOUSBCompositeDriver/Headers/IOUSBCompositeDriver.h"

if [ ${#diverged[@]} -ne 0 ]; then
	echo "error: these vendored files differ from their source AND have" >&2
	echo "       uncommitted edits in the XNU tree. Refusing to overwrite:" >&2
	for f in "${diverged[@]}"; do echo "         git -C $XNU diff -- $f" >&2; done
	echo "       Move the fix into anurodhp/IOHIDFamily (iokit-port), tools/patch_iohid_gaps.sh (IOUSBHIDDriver) or compat/iohid/, then re-run." >&2
	exit 1
fi

# ---------------------------------------------------------------------
# iokit/conf/files block (regenerated between the marker and the next
# blank line on every run).
# ---------------------------------------------------------------------
MARKER="# --- HID (IOHIDFamily-1633.81.2 + IOUSBHIDDriver), vendored by tools/vendor_iohid_into_xnu.sh ---"
CONFBLOCK="$(mktemp)"
{
	echo "$MARKER"
	for s in $FAM_SRCS; do printf 'iokit/HID/IOHIDFamily/%s.cpp\t\toptional iokitcpp\n' "$s"; done
	printf 'iokit/HID/IOHIDFamily/IOHIDEventDummyService/IOHIDEventDummyService.cpp\t\toptional iokitcpp\n'
	for s in $SYS_SRCS; do printf 'iokit/HID/IOHIDSystem/%s.cpp\t\toptional iokitcpp\n' "$s"; done
	printf 'iokit/HID/IOHIDFamily/Cosmo_USB2ADB.c\t\toptional iokitcpp\n'
	for f in "$D_PARSER"/*.c; do
		printf 'iokit/HID/IOHIDSystem/IOHIDDescriptorParser/%s\t\toptional iokitcpp\n' "$(basename "$f")"
	done
	for s in $DK_IMPLS; do printf 'iokit/HID/HIDDriverKit/%s.iig.cpp\t\toptional iokitcpp\n' "$s"; done
	printf 'iokit/HID/IOUSBHIDDriver/IOUSBHIDDriver.cpp\t\toptional iokitcpp\n'
	printf 'iokit/HID/IOUSBHIDDriver/IOUSBCompositeDriver.cpp\t\toptional iokitcpp\n'
} > "$CONFBLOCK"
python3 - "$CONF" "$CONFBLOCK" "$MARKER" <<'PYEOF'
import sys
conf, blockf, marker = sys.argv[1:4]
lines = open(conf).read().split("\n")
block = open(blockf).read().rstrip("\n").split("\n")
if marker in lines:
    a = lines.index(marker)
    b = a
    while b < len(lines) and lines[b].strip() != "":
        b += 1
    lines = lines[:a] + block + lines[b:]
else:
    while lines and lines[-1] == "":
        lines.pop()
    lines += [""] + block + [""]
open(conf, "w").write("\n".join(lines))
PYEOF
rm -f "$CONFBLOCK"

# Every vendored source must be listed (else it silently never compiles),
# and collect the object names for the flags block.
HID_CXX_OBJS=""
HID_C_OBJS=""
missing=()
while IFS= read -r f; do
	rel="${f#$XNU/}"
	case "$rel" in
		iokit/HID/IOHIDFamily/AppleUser*) continue ;;   # not built, see header
	esac
	LC_ALL=C grep -q "^$rel[[:space:]]" "$CONF" || missing+=("$rel")
	case "$f" in
		*.cpp) HID_CXX_OBJS="$HID_CXX_OBJS $(basename "${f%.cpp}").cpo" ;;
		*.c)   HID_C_OBJS="$HID_C_OBJS $(basename "${f%.c}").o" ;;
	esac
done < <(find "$D" -name '*.cpp' -o -name '*.c' | LC_ALL=C sort)
if [ ${#missing[@]} -ne 0 ]; then
	echo "error: vendored but not listed in $CONF (would never compile):" >&2
	printf '  %s\n' "${missing[@]}" >&2
	exit 1
fi
# A stale AppleUser* copy from an earlier run would otherwise sit unbuilt
# but visible; keep the tree matching the list.
rm -f "$D_FAM"/AppleUserHIDDevice.cpp "$D_FAM"/AppleUserHIDEventService.cpp

# Object basenames must be unique kernel-wide (xnu names objects by basename).
for o in $HID_CXX_OBJS $HID_C_OBJS; do
	src="${o%.cpo}"; src="${src%.o}"
	n=$(LC_ALL=C grep -cE "/$(printf '%s' "$src" | sed 's/\./\\./g')\.(c|cpp|s)[[:space:]]" "$XNU"/*/conf/files || true)
	n=$(echo "$n" | awk -F: '{s+=$NF} END {print s+0}')
	if [ "$n" != 1 ]; then
		echo "error: object name $o is listed $n times across */conf/files" >&2
		exit 1
	fi
done

# Our -I dirs precede the kernel's own INCFLAGS, so a bare-name header in
# them that also exists on the kernel include path would silently win.
shadowed=()
for d in "$D_COMPAT" "$D_FAM" "$D_SYS" "$D_PARSER" "$H_HIDSYS" "$XNU/iokit/USB"; do
	for h in "$d"/*.h; do
		[ -f "$h" ] || continue
		b="$(basename "$h")"
		for r in "$XNU/iokit" "$XNU/iokit/Network" "$XNU/bsd" "$XNU/osfmk" \
			 "$XNU/EXTERNAL_HEADERS" "$XNU"/BUILD/obj/EXPORT_HDRS \
			 "$XNU"/BUILD/obj/EXPORT_HDRS/*; do
			[ -f "$r/$b" ] && shadowed+=("$b: ${d#$XNU/} shadows ${r#$XNU/}")
		done
	done
done
if [ ${#shadowed[@]} -ne 0 ]; then
	echo "error: a vendored header would shadow a kernel header of the same name:" >&2
	printf '  %s\n' "${shadowed[@]}" >&2
	exit 1
fi

# ---------------------------------------------------------------------
# iokit/conf/Makefile.template: per-object flags for exactly these objects.
# ---------------------------------------------------------------------
BEGIN="# BEGIN IOHIDFamily per-object flags -- generated by tools/vendor_iohid_into_xnu.sh, do not edit"
END="# END IOHIDFamily per-object flags"
BLOCK="$(mktemp)"
trap 'rm -f "$BLOCK"' EXIT
{
	echo "$BEGIN"
	echo "#"
	echo "# APPLE_KEXT_ASSERTIONS=1: IOHIDFamily.xcconfig GCC_PREPROCESSOR_DEFINITIONS."
	echo "# The force-include and the -I dirs mirror tools/iohid_gap_audit.sh; see"
	echo "# tools/vendor_iohid_into_xnu.sh. -I dirs go in _CFLAGS_ADD, not"
	echo "# _INCFLAGS (P_RULE_1A fuses _INCFLAGS with the source path)."
	echo "IOHID_CXX_OBJS =$HID_CXX_OBJS"
	echo "IOHID_C_OBJS =$HID_C_OBJS"
	echo "IOHID_CFLAGS = -DAPPLE_KEXT_ASSERTIONS=1 \\"
	echo "	-include \$(SRCROOT)/iokit/HID/compat/IOHIDFamilyPrivateMissing.h \\"
	echo "	-I\$(SRCROOT)/iokit/HID/compat \\"
	echo "	-I\$(SRCROOT)/iokit/HID/IOHIDFamily \\"
	echo "	-I\$(SRCROOT)/iokit/HID/IOHIDSystem \\"
	echo "	-I\$(SRCROOT)/iokit/IOKit/hidsystem \\"
	echo "	-I\$(SRCROOT)/iokit/USB"
	echo "\$(foreach o,\$(IOHID_CXX_OBJS) \$(IOHID_C_OBJS),\$(eval \$(o)_CFLAGS_ADD += \$(IOHID_CFLAGS)))"
	echo "$END"
} > "$BLOCK"
python3 - "$MKT" "$BLOCK" "$BEGIN" "$END" <<'PYEOF'
import sys
mkt, blockf, begin, end = sys.argv[1:5]
s = open(mkt).read()
block = open(blockf).read()
if begin in s:
    a = s.index(begin)
    b = s.index(end, a) + len(end) + 1
    s = s[:a] + block + s[b:]
else:
    anchor = "\n%RULES\n"
    if s.count(anchor) != 1:
        sys.exit("error: %RULES anchor not found exactly once in " + mkt)
    s = s.replace(anchor, "\n" + block + anchor)
open(mkt, "w").write(s)
PYEOF
if [ "$(grep -c "^$BEGIN$" "$MKT")" != 1 ]; then
	echo "error: IOHIDFamily flags block did not apply exactly once to $MKT" >&2
	exit 1
fi

echo "=== IOHIDFamily vendored into $XNU ==="
echo "  C++ objects: $(echo $HID_CXX_OBJS | wc -w | tr -d ' '), C objects: $(echo $HID_C_OBJS | wc -w | tr -d ' ')"
