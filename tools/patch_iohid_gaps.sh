#!/bin/bash
#
# Patches the real compile gaps in the pinned Apple IOHIDFamily drop
# (third_party/IOHIDFamily, IOHIDFamily-1633.81.2) that
# tools/iohid_gap_audit.sh finds, and that a force-included header cannot
# fix. DAR-245. Same model as tools/patch_iographics_gaps.sh:
# third_party/ is gitignored and re-cloned, so every fix must be
# reproducible against a fresh clone, and the vendoring step copies the
# already-patched checkout.
#
# Declarations that are merely MISSING (private keys, unpublished
# usages) live in compat/iohid/IOHIDFamilyPrivateMissing.h instead. Read
# that header for how each value was sourced.
#
# Each fix cites its evidence (DAR-313 convention). The script is idempotent,
# and every edit asserts on its own target so a re-pin fails loudly
# instead of silently not applying.

set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

HID="third_party/IOHIDFamily"
F="$HID/IOHIDFamily"
S="$HID/IOHIDSystem"

if [ ! -f "$F/IOHIDDevice.cpp" ]; then
	echo "error: $F not found - run ./setup_third_party.sh" >&2
	exit 1
fi

# Apple's 2012-2020 sources carry latin-1 bytes; see build_iousbfamily.sh.
export LC_ALL=C

# replace_once <file> <literal-old> <literal-new>
#   Already-new -> no-op. Old present -> replaced. Neither -> hard error.
replace_once() {
	python3 - "$@" <<'PYEOF'
import sys
path, old, new = sys.argv[1], sys.argv[2], sys.argv[3]
s = open(path, 'rb').read().decode('latin-1')
if new in s and old not in s:
    sys.exit(0)
n = s.count(old)
if n != 1:
    sys.exit("error: %s: expected exactly 1 occurrence of %r, found %d" % (path, old, n))
open(path, 'wb').write(s.replace(old, new).encode('latin-1'))
PYEOF
}

# ---------------------------------------------------------------------
# 1. AppleHIDUsageTables.h <- Apple's last real published version.
#
# Every tag from IOHIDFamily-1090.200.63 to 2238.120.5 publishes this
# header as a 22-line, license-only stub. The last tag with real content
# is IOHIDFamily-1035.70.7 (macOS 10.13), 268 lines, which defines 30 of the
# 51 Apple-vendor usage identifiers the 1633 sources use
# (kHIDPage_AppleVendor = 0xFF00, kHIDPage_AppleVendorKeyboard = 0xFF01,
# kHIDPage_AppleVendorTopCase = 0x00FF, ...). Usage values are part of a
# device's wire protocol, so a later release cannot renumber one without
# breaking already-shipped hardware. Using the older file is therefore
# faithful. The 21 identifiers added after 10.13 are in the compat header.
#
# Fetched from the real tag rather than checked into this repo, the same
# way every other Apple source reaches third_party/.
# ---------------------------------------------------------------------
USAGE_H="$F/AppleHIDUsageTables.h"
USAGE_TAG="IOHIDFamily-1035.70.7"
if ! grep -q 'kHIDPage_AppleVendor[[:space:]]*=' "$USAGE_H"; then
	if [ "$(grep -c . "$USAGE_H")" -gt 30 ]; then
		echo "error: $USAGE_H is no longer the license-only stub this fix" >&2
		echo "       replaces. The pin changed; re-check before overwriting." >&2
		exit 1
	fi
	git -C "$HID" rev-parse -q --verify "refs/tags/$USAGE_TAG" >/dev/null || \
		git -C "$HID" fetch -q --depth 1 origin "refs/tags/$USAGE_TAG:refs/tags/$USAGE_TAG"
	git -C "$HID" show "$USAGE_TAG:IOHIDFamily/AppleHIDUsageTables.h" > "$USAGE_H.tmp"
	grep -q 'kHIDPage_AppleVendor[[:space:]]*= 0xFF00' "$USAGE_H.tmp" || {
		echo "error: $USAGE_TAG AppleHIDUsageTables.h lacks kHIDPage_AppleVendor" >&2
		rm -f "$USAGE_H.tmp"; exit 1; }
	mv "$USAGE_H.tmp" "$USAGE_H"
	echo "+ AppleHIDUsageTables.h <- $USAGE_TAG"
fi

# ---------------------------------------------------------------------
# 2. Stray semicolon before APPLE_KEXT_OVERRIDE.
#
#   AppleUserHIDDevice.h:134       virtual void stop(IOService * provider); APPLE_KEXT_OVERRIDE;
#   AppleUserHIDEventService.h:51  virtual bool terminate(IOOptionBits options = 0); APPLE_KEXT_OVERRIDE;
#
# APPLE_KEXT_OVERRIDE is `override` for any C++11 kernel build
# (xnu-7195 libkern/libkern/c++/OSMetaClass.h:225-226), so the second
# `;` leaves a bare `override;` member declaration: "a type specifier is
# required", which then breaks the whole class (144 + 55 cascading
# errors in the two .cpp files). The intent, marking the override, is
# unambiguous: every sibling declaration in both headers is written
# `...) APPLE_KEXT_OVERRIDE;`.
# ---------------------------------------------------------------------
replace_once "$F/AppleUserHIDDevice.h" \
	'virtual void stop(IOService * provider); APPLE_KEXT_OVERRIDE;' \
	'virtual void stop(IOService * provider) APPLE_KEXT_OVERRIDE;'
replace_once "$F/AppleUserHIDEventService.h" \
	'virtual bool terminate(IOOptionBits options = 0); APPLE_KEXT_OVERRIDE;' \
	'virtual bool terminate(IOOptionBits options = 0) APPLE_KEXT_OVERRIDE;'

# ---------------------------------------------------------------------
# 3. NULL used as an integer.
#
# On arm64 C++11, NULL is nullptr (xnu-7195 iokit/IOKit/IOTypes.h:51-58),
# which cannot convert to an integer. Same class as
# tools/build_iousbfamily.sh's `UInt32 info = NULL` fix. 0 is exactly
# what each site means:
#   IOHIDDevice.cpp:1180/1197/2043: `bmd ? (uintptr_t)... : NULL` is a
#     hid_trace() argument, so 0 is the "no buffer" trace value.
#   IOHIDevice.cpp:123: `SInt32 GenerateKey(...) { if (!object) return NULL;`
#     returns 0 as the key for a null object.
# ---------------------------------------------------------------------
python3 - "$F/IOHIDDevice.cpp" <<'PYEOF'
import sys
p = sys.argv[1]
s = open(p, 'rb').read().decode('latin-1')
old = "(uintptr_t)bmd->getBytesNoCopy() : NULL"
new = "(uintptr_t)bmd->getBytesNoCopy() : 0"
n = s.count(old)
if n == 0 and s.count(new) == 3:
    sys.exit(0)
if n != 3:
    sys.exit("error: %s: expected 3 `bmd ? ... : NULL` trace sites, found %d" % (p, n))
open(p, 'wb').write(s.replace(old, new).encode('latin-1'))
PYEOF
replace_once "$S/IOHIDevice.cpp" \
	$'if ( !object )\n        return NULL;' \
	$'if ( !object )\n        return 0;'

# Same NULL-as-integer class, two more sites:
#   AppleUserHIDDevice.cpp:722: the same `bmd ? ... : NULL` hid_trace
#     argument as IOHIDDevice.cpp above.
#   IOHIDEventDriver.cpp:2176: `UInt32 value = NULL;`, a local that is
#     assigned before use, so the initializer is simply 0.
replace_once "$F/AppleUserHIDDevice.cpp" \
	'(uintptr_t)bmd->getBytesNoCopy() : NULL' \
	'(uintptr_t)bmd->getBytesNoCopy() : 0'
replace_once "$F/IOHIDEventDriver.cpp" \
	'UInt32 value                        = NULL;' \
	'UInt32 value                        = 0;'

# ---------------------------------------------------------------------
# 4. IOHIDEventServiceFastPathUserClient.{h,cpp} <- IOHIDFamily-1633.60.4.
#
# Both files are in the IOHIDFamily kext target's Sources/Headers phases,
# and IOHIDEventService.cpp includes the header (:59) and instantiates the class
# (:2358, newUserClient for kIOHIDEventServiceFastPathUserClientType). But
# 1633.81.2 does not publish either file. 1633.60.4 is the tag
# immediately before it in the same macOS 11 train and publishes both
# (h 5531 bytes, cpp 23897 bytes; 1633.40.25 is byte-identical in size).
# Neither file has a TARGET_OS_* conditional, so the macOS-unifdef of the
# 81.2 drop (see the note in tools/iohid_gap_audit.sh) does not affect them.
# ---------------------------------------------------------------------
FP_TAG="IOHIDFamily-1633.60.4"
for fp in IOHIDEventServiceFastPathUserClient.h IOHIDEventServiceFastPathUserClient.cpp; do
	if [ ! -s "$F/$fp" ]; then
		git -C "$HID" rev-parse -q --verify "refs/tags/$FP_TAG" >/dev/null || \
			git -C "$HID" fetch -q --depth 1 origin "refs/tags/$FP_TAG:refs/tags/$FP_TAG"
		git -C "$HID" show "$FP_TAG:IOHIDFamily/$fp" > "$F/$fp"
		echo "+ $fp <- $FP_TAG"
	fi
done

# ---------------------------------------------------------------------
# 5. APPLE_KEXT_OVERRIDE on a non-virtual (the 1633.60.4 header from fix 4).
#
# IOHIDEventServiceFastPathUserClient.h:101 marks clientMemoryForTypeGated()
# APPLE_KEXT_OVERRIDE, i.e. `override` (OSMetaClass.h:225-226), but it is a
# private gated helper that overrides nothing: IOUserClient has no such
# virtual (xnu-7195 iokit/IOKit/IOUserClient.h), and the three sibling
# user clients in this same drop declare the identical helper with no
# marker (IOHIDEventServiceUserClient.h:128, IOHIDResourceUserClient.h:192,
# IOHIDLibUserClient.h:253). Dropping the marker changes nothing at runtime.
# ---------------------------------------------------------------------
replace_once "$F/IOHIDEventServiceFastPathUserClient.h" \
	'IOReturn clientMemoryForTypeGated (IOOptionBits * options, IOMemoryDescriptor ** memory) APPLE_KEXT_OVERRIDE;' \
	'IOReturn clientMemoryForTypeGated (IOOptionBits * options, IOMemoryDescriptor ** memory);'

# ---------------------------------------------------------------------
# 6. IOUSBHIDDriver.cpp (IOUSBFamily-560.4.2) -> staged, fixed copy.
#
# third_party/IOUSBFamily stays pristine (tools/build_iousbfamily.sh's
# convention), so this fix goes on a copy under build/iohid/src/, which
# the audit and the vendoring step both compile.
#
# :2772 `case -1:` in a switch on `UInt32 direction`: a C++11 narrowing
# error. The callers pass `(void*)-1` (:2807) and `(void*)1` (:2826),
# and `direction = (uintptr_t)param1` (:2758) truncates the former to
# 0xFFFFFFFF, which is exactly (UInt32)-1. Under the C++98 rules the file
# was written for, the case label converted implicitly to that same value.
# ---------------------------------------------------------------------
USBHID_SRC="third_party/IOUSBFamily/IOUSBHIDDriver/Classes/IOUSBHIDDriver.cpp"
USBHID_OUT="build/iohid/src/IOUSBHIDDriver.cpp"
mkdir -p "$(dirname "$USBHID_OUT")"
cp "$USBHID_SRC" "$USBHID_OUT"
replace_once "$USBHID_OUT" \
	$'\t\tcase -1:\n\t\t\tif (!--me->_outstandingIO' \
	$'\t\tcase (UInt32)-1:\t/* was -1; see tools/patch_iohid_gaps.sh fix 6 */\n\t\t\tif (!--me->_outstandingIO'

echo "=== patch_iohid_gaps.sh: IOHIDFamily gaps patched ==="
