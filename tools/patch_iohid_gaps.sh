#!/bin/bash
#
# Stages the fixed IOUSBHIDDriver.cpp copy (IOUSBFamily-560.4.2) that
# tools/iohid_gap_audit.sh and tools/vendor_iohid_into_xnu.sh compile.
# DAR-245.
#
# IOHIDFamily's own source fixes (old fixes 1-5: the AppleHIDUsageTables.h
# restore from 1035.70.7, the stray `; APPLE_KEXT_OVERRIDE;`, NULL used as
# an integer, the FastPath user client from 1633.60.4, and its bogus
# override marker) are NOT here anymore. They are commits on this
# project's fork, anurodhp/IOHIDFamily branch iokit-port
# (IOHIDFamily-1633.81.2 plus one commit per fix, each with its citation
# in the commit message; DAR-356), which setup_third_party.sh clones. This
# script only checks that the checkout is that fork.
#
# What remains are fixes 6/6b. They go on a STAGED COPY of an IOUSBFamily
# file, because third_party/IOUSBFamily is still a plain upstream pin with
# no fork (same convention as tools/build_iousbfamily.sh). Moving these
# into an anurodhp/IOUSBFamily fork is tracked separately in Linear
# (gitrepo label). Numbering is kept as 6/6b because the staged file's
# own comments cite "fix 6" / "fix 6b".
#
# Declarations that are merely MISSING (private keys, unpublished
# usages) live in compat/iohid/IOHIDFamilyPrivateMissing.h instead.
#
# Each fix cites its evidence (DAR-313 convention). The script is idempotent,
# and every edit asserts on its own target so a re-pin fails loudly
# instead of silently not applying.

set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

HID="third_party/IOHIDFamily"
F="$HID/IOHIDFamily"

if [ ! -f "$F/IOHIDDevice.cpp" ]; then
	echo "error: $F not found - run ./setup_third_party.sh" >&2
	exit 1
fi

# The IOHIDFamily checkout must be the anurodhp/IOHIDFamily fork. Check the
# first and last fix commits' results so a plain upstream clone fails
# here, not with hundreds of compile errors later.
if ! grep -q 'kHIDPage_AppleVendor[[:space:]]*= 0xFF00' "$F/AppleHIDUsageTables.h" ||
   [ ! -s "$F/IOHIDEventServiceFastPathUserClient.cpp" ]; then
	echo "error: $HID lacks this project's fixes -- it is not the" >&2
	echo "       anurodhp/IOHIDFamily iokit-port fork. Re-clone it:" >&2
	echo "       rm -rf $HID && ./setup_third_party.sh" >&2
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

# 6b. Same staged copy. IOUSBHIDDriver::GetIndexedString() (:2397):
#
#     strlcpy((char *)vOutBuf, strBuf, (size_t)strlen);
#
# `strlen` there is the libc FUNCTION, not the local `strLen` two lines
# above it, so the size argument is a kernel text address. Apple's build
# got away with it: strlcpy stops at strBuf's NUL, and the `outSize < strLen`
# check just above already guarantees the destination fits. This kernel
# builds with _FORTIFY_SOURCE, so it becomes __strlcpy_chk, which compares
# that "size" against the real object size and panics. That really happened
# on the first QEMU boot with a usb-kbd attached: "__strlcpy_chk object size
# check failed: dst ..., (256 < 18446744005109157696)" from
# IOUSBHIDDriver::newManufacturerString() <- IOHIDDevice::publishProperties.
# The intended size is strLen: the string length plus the NUL, already
# bounds-checked against the caller's buffer.
replace_once "$USBHID_OUT" \
	'strlcpy((char *)vOutBuf, strBuf, (size_t)strlen);' \
	'strlcpy((char *)vOutBuf, strBuf, (size_t)strLen);	/* was (size_t)strlen, the function; see tools/patch_iohid_gaps.sh fix 6b */'

echo "=== patch_iohid_gaps.sh: IOUSBHIDDriver.cpp staged (fixes 6/6b) ==="
