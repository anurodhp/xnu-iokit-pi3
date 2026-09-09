#!/bin/bash
#
# Vendors real Apple IOUSBFamily and AppleUSBCDCDriver into the kernel
# source tree so they are COMPILED INTO the kernel, following exactly the
# precedent already set for IONetworkingFamily and IOStorageFamily:
#
#   third_party/xnu-7195/iokit/Network/   <- IONetworkingFamily sources
#   third_party/xnu-7195/iokit/Storage/   <- IOStorageFamily sources
#   third_party/xnu-7195/iokit/IOKit/network|storage/  <- their headers
#   + one line per .cpp in third_party/xnu-7195/iokit/conf/files
#
# This script adds the same for USB:
#
#   third_party/xnu-7195/iokit/USB/               <- IOUSBFamily sources
#   third_party/xnu-7195/iokit/IOKit/usb/         <- IOUSBFamily headers
#   third_party/xnu-7195/iokit/IOKit/pci/         <- IOPCIFamily headers
#   third_party/xnu-7195/iokit/IOKit/serial/      <- IOSerialFamily headers
#   third_party/xnu-7195/iokit/IOKit/acpi/        <- our ARM stand-in
#   third_party/xnu-7195/iokit/Drivers/AppleUSBCDC/ <- CDC-ECM drivers
#
# WHY A COPY RATHER THAN AN INCLUDE PATH: because that is what this tree
# already does, and because xnu's own build system builds what is under
# its own SRCROOT -- iokit/conf/files paths are all relative to it. Making
# the kernel build reach outside SRCROOT into third_party/ would be a new
# pattern for one family's benefit.
#
# IMPORTANT: the copies under third_party/xnu-7195 are GENERATED. Do not
# hand-edit them -- rerunning this script overwrites them. A real local
# fix belongs in the FIXES section below (so it is reproducible and
# reviewable), the same way tools/build_iousbfamily.sh keeps its one fix.
# The script refuses to clobber a vendored file that has uncommitted edits
# in the xnu tree, for the same reason tools/sync_iokit_drivers.sh does:
# that check already caught a real correction that would otherwise have
# been silently destroyed.
#
# Run directly, or let tools/build_xnu_7195.sh call it (it does).
#
# Idempotent.

set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

XNU="third_party/xnu-7195"
FAMILY="third_party/IOUSBFamily"
CDC="third_party/AppleUSBCDCDriver"
CONF="$XNU/iokit/conf/files"

if [ ! -d "$XNU/iokit/conf" ]; then
	echo "error: $XNU not found - run ./setup_third_party.sh" >&2
	exit 1
fi
for d in "$FAMILY/IOUSBFamily/Classes" "$CDC/AppleUSBCDCECM" \
	 third_party/IOPCIFamily/IOKit/pci \
	 third_party/IOSerialFamily/IOSerialFamily.kmodproj; do
	if [ ! -d "$d" ]; then
		echo "error: $d not found - run ./setup_third_party.sh" >&2
		exit 1
	fi
done
if [ ! -s "$FAMILY/IOUSBFamily/Classes/IOUSBController.cpp" ]; then
	echo "error: IOUSBController.cpp is EMPTY -- third_party/IOUSBFamily is" >&2
	echo "       pinned to a tag that publishes zero-byte sources (630.4.5" >&2
	echo "       does). Re-pin to IOUSBFamily-560.4.2." >&2
	exit 1
fi

USB_SRC="$XNU/iokit/USB"
USB_HDR="$XNU/iokit/IOKit/usb"
PCI_HDR="$XNU/iokit/IOKit/pci"
SER_HDR="$XNU/iokit/IOKit/serial"
ACPI_HDR="$XNU/iokit/IOKit/acpi"
CDC_SRC="$XNU/iokit/Drivers/AppleUSBCDC"

mkdir -p "$USB_SRC" "$USB_HDR" "$PCI_HDR" "$SER_HDR" "$ACPI_HDR" "$CDC_SRC"

# Refuse to overwrite a vendored file that has uncommitted edits on the
# xnu-tree side -- someone may have been debugging there and a blind copy
# would destroy it. Same guard, same reasoning, as sync_iokit_drivers.sh.
diverged=()
check_divergence() {
	local dst="$1" src="$2"
	[ -e "$dst" ] || return 0
	cmp -s "$src" "$dst" && return 0
	local rel="${dst#$XNU/}"
	if ! git -C "$XNU" diff --quiet -- "$rel" 2>/dev/null; then
		diverged+=("$rel")
	fi
}

copy_into() {
	# copy_into <destdir> <files...>
	local dst="$1"; shift
	local f
	for f in "$@"; do
		check_divergence "$dst/$(basename "$f")" "$f"
	done
	for f in "$@"; do
		cp "$f" "$dst/$(basename "$f")"
	done
}

# --- IOUSBFamily -----------------------------------------------------
copy_into "$USB_SRC" "$FAMILY/IOUSBFamily/Classes"/*.cpp
# The family's own sources include some of their headers with "" rather
# than <IOKit/usb/...>: USBTracepoints.h, IOUSBFamilyInfoPlist.pch and
# IOUSBInterfaceUserClient.h. Those resolve relative to the .cpp, so they
# have to sit alongside the sources as well as in IOKit/usb/.
copy_into "$USB_SRC" "$FAMILY/IOUSBFamily/Headers"/*.h
copy_into "$USB_SRC" "$FAMILY/IOUSBUserClient/Headers"/*.h
copy_into "$USB_SRC" "$FAMILY/IOUSBFamilyInfoPlist.pch"
copy_into "$USB_HDR" "$FAMILY/IOUSBFamily/Headers"/*.h
copy_into "$USB_HDR" "$FAMILY/IOUSBUserClient/Headers"/*.h

# --- IOPCIFamily / IOSerialFamily / ACPI headers ----------------------
# Headers only. Neither PCI nor serial hardware exists on this board; see
# setup_third_party.sh's comments for why the declarations still have to
# resolve. IOACPIPlatformDevice.h is ours (IOACPIFamily was never open
# sourced) -- see compat/IOKit/acpi/IOACPIPlatformDevice.h's header.
copy_into "$PCI_HDR" third_party/IOPCIFamily/IOKit/pci/*.h
copy_into "$SER_HDR" \
	third_party/IOSerialFamily/IOSerialFamily.kmodproj/IOModemSerialStreamSync.h \
	third_party/IOSerialFamily/IOSerialFamily.kmodproj/IORS232SerialStreamSync.h \
	third_party/IOSerialFamily/IOSerialFamily.kmodproj/IOSerialDriverSync.h \
	third_party/IOSerialFamily/IOSerialFamily.kmodproj/IOSerialKeys.h \
	third_party/IOSerialFamily/IOSerialFamily.kmodproj/IOSerialStreamSync.h \
	third_party/IOSerialFamily/IOSerialFamily.kmodproj/ioss.h
copy_into "$ACPI_HDR" compat/IOKit/acpi/IOACPIPlatformDevice.h

# --- our own glue ----------------------------------------------------
# The 6 symbols the family and the CDC drivers reference that this board
# cannot provide (5 IOPCIDevice + kmod_info). See the file's own header
# for why the obvious one-line stub for IOPCIDevice::metaClass is a
# null-deref panic rather than a shortcut.
copy_into "$USB_SRC" compat/usb/BCM2837USBKernelGlue.cpp

# --- AppleUSBCDCDriver ------------------------------------------------
copy_into "$CDC_SRC" \
	"$CDC/AppleUSBCDC/Classes/AppleUSBCDC.cpp" \
	"$CDC/AppleUSBCDCECM/ControlDriver/Classes/AppleUSBCDCECMControl.cpp" \
	"$CDC/AppleUSBCDCECM/DataDriver/Classes/AppleUSBCDCECMData.cpp" \
	"$CDC/Common/MsgTrace.c" \
	"$CDC/Common/AppleUSBCDCCommon.h" \
	"$CDC/Common/XTrace.h" \
	"$CDC/AppleUSBCDC/Headers/AppleUSBCDC.h" \
	"$CDC/AppleUSBCDC/Headers/AppleUSBCDCPrivate.h" \
	"$CDC/AppleUSBCDC/Headers/WWANSchemaDefinitions.h" \
	"$CDC/AppleUSBCDCECM/Common/AppleUSBCDCECM.h" \
	"$CDC/AppleUSBCDCECM/ControlDriver/Headers/AppleUSBCDCECMControl.h" \
	"$CDC/AppleUSBCDCECM/DataDriver/Headers/AppleUSBCDCECMData.h" \
	"$CDC/AppleUSBCDCECM/DataDriver/Headers/linkup.h"

if [ ${#diverged[@]} -ne 0 ]; then
	echo "error: these vendored files differ from their third_party/ source" >&2
	echo "       AND have uncommitted edits in the XNU tree, so the XNU-tree" >&2
	echo "       copy may hold real work. Refusing to overwrite:" >&2
	for f in "${diverged[@]}"; do
		echo "         git -C $XNU diff -- $f" >&2
	done
	echo "       Move the fix into this script's FIXES section, then re-run." >&2
	exit 1
fi

# ---------------------------------------------------------------------
# FIXES -- the only edits made to Apple's published source, each a real
# bug under C++11+ rules rather than a porting compromise. Both assert on
# their own target and re-check afterwards, so a re-pin that changes the
# line fails loudly instead of silently not applying.
#
# LC_ALL=C is load-bearing on every grep/sed here: Apple's 2012 sources
# carry a latin-1 (c) byte that makes BSD sed abort with "RE error:
# illegal byte sequence" and makes grep treat the file as binary and
# match nothing.
# ---------------------------------------------------------------------

# 1. IOUSBDevice.cpp: `UInt32 info = NULL;` -- an out-parameter for
#    GetPortInformation(), overwritten before every read. Legal in C++03,
#    a hard error under -std=gnu++14 (cannot initialize 'UInt32' from
#    'nullptr_t').
F="$USB_SRC/IOUSBDevice.cpp"
if LC_ALL=C grep -q 'UInt32[[:space:]]*info = NULL;' "$F"; then
	LC_ALL=C sed -i '' \
		's|UInt32\([[:space:]]*\)info = NULL;|UInt32\1info = 0;\t/* was NULL; see tools/vendor_usb_into_xnu.sh */|' "$F"
fi
if LC_ALL=C grep -q 'UInt32[[:space:]]*info = NULL;' "$F"; then
	echo "error: IOUSBDevice.cpp NULL fix did not apply" >&2
	exit 1
fi

# 1b. IOUSBPipe.cpp: `bool IOUSBPipe::InitToEndpoint(...)` -- one of the
#     file's four deprecated no-op stubs -- ends `return NULL;`. `false`
#     is plainly what it means.
#
#     Worth noting WHY this one is here and not in
#     tools/build_iousbfamily.sh's fix list: that script compiles with
#     Xcode 12 at -std=gnu++14, where NULL is __null and converts to bool
#     silently. The real kernel build uses the LIVE Xcode against xnu's
#     own C++ dialect, where NULL is nullptr and copy-initialising a bool
#     from std::nullptr_t is a hard error:
#
#       error: cannot initialize return object of type 'bool' with an
#              rvalue of type 'std::nullptr_t'
#
#     So a file can compile perfectly under build_iousbfamily.sh and
#     still fail the kernel build. The standalone script is a fast
#     syntax gate, NOT a proxy for the real build -- do not treat a clean
#     19/19 there as proof the kernel will link.
#
#     Checked for other instances of the same pattern across every
#     vendored USB/CDC source (a bool-returning function with `return
#     NULL;`): this is the only one.
F="$USB_SRC/IOUSBPipe.cpp"
if LC_ALL=C grep -q 'IOUSBPipe::InitToEndpoint' "$F"; then
	LC_ALL=C sed -i '' \
		'/IOUSBPipe::InitToEndpoint, obsolete method 1 called/,/^}/ s|    return NULL;|    return false;\t/* was NULL; see tools/vendor_usb_into_xnu.sh */|' "$F"
fi
if ! LC_ALL=C grep -q 'return false;.*was NULL' "$F"; then
	echo "error: IOUSBPipe.cpp bool-return fix did not apply" >&2
	exit 1
fi

# 1c. IOUSBControllerV3.h declares an override that NOTHING DEFINES:
#
#       // we override this one to add some stuff which requires the _device iVar
#       virtual UInt32 GetErrataBits(UInt16 vendorID, UInt16 deviceID, UInt16 revisionID);
#
#     There is no IOUSBControllerV3::GetErrataBits anywhere in
#     IOUSBFamily-560.4.2 -- checked every file in the repo, not just the
#     vendored subset. The only definitions are
#     IOUSBController::GetErrataBits (IOUSBController_Errata.cpp:131, the
#     real errata-table lookup) and AppleUSBXHCI's own, on a different
#     class. The declaration is vestigial: Apple's comment describes an
#     override that was never published.
#
#     It only bites at the kernel link, because a vtable is emitted just
#     once (in the TU holding the key function) and LTO is what
#     materialises it:
#
#       Undefined symbols for architecture arm64:
#         "IOUSBControllerV3::GetErrataBits(unsigned short, unsigned short,
#          unsigned short)", referenced from:
#             vtable for IOUSBControllerV3 in justlto.o
#
#     Per-file compiles never reference the vtable, so both
#     tools/build_iousbfamily.sh AND the per-object nm sweep came back
#     clean -- a missing *virtual* definition is invisible to both.
#
#     Deleting the declaration is the fix, not writing a body: with it
#     gone, IOUSBControllerV3 inherits IOUSBController::GetErrataBits,
#     which is real, published, and does the actual lookup. Inventing an
#     override would mean inventing behaviour Apple never shipped source
#     for. On this board it returns no errata anyway -- the lookup keys
#     off PCI vendor/device/revision IDs, and there is no PCI bus.
#     Applied to both vendored copies of the header.
for F in "$USB_HDR/IOUSBControllerV3.h" "$USB_SRC/IOUSBControllerV3.h"; do
	if LC_ALL=C grep -q 'virtual UInt32.*GetErrataBits' "$F"; then
		LC_ALL=C sed -i '' \
			's|^\(.*virtual UInt32.*GetErrataBits.*\)$|/* declaration removed by tools/vendor_usb_into_xnu.sh -- no definition exists in IOUSBFamily-560.4.2; inherit IOUSBController::GetErrataBits */\n// \1|' "$F"
	fi
	if LC_ALL=C grep -q '^[[:space:]]*virtual UInt32.*GetErrataBits' "$F"; then
		echo "error: IOUSBControllerV3.h GetErrataBits removal did not apply to $F" >&2
		exit 1
	fi
done

# 1d. IOUSBController.cpp double-registers its debug sysctl, which
#     PANICS THE KERNEL AT BOOT once this code is built in rather than
#     loaded as a kext:
#
#       panic(cpu 0 caller ...): "attempting to register a sysctl at
#                                 previously registered slot : 154"
#       ... __GLOBAL__sub_I_IOUSBController.cpp
#
#     The file declares `SYSCTL_PROC(_debug, OID_AUTO, USB, ...)` AND
#     calls sysctl_register_oid(&sysctl__debug_USB) from the constructor
#     of its static `gUSBStackGlobals` (USBGlobals::USBGlobals), with the
#     matching unregister in the destructor.
#
#     For a kext that is correct and necessary: a kext's SYSCTL_PROC
#     linker-set entry is not in the kernel's own `__sysctl_set`, so
#     nothing registers it automatically and the constructor must.
#     Compiled INTO the kernel the entry lands in `__sysctl_set`,
#     bsd/kern/kern_newsysctl.c:400's sysctl_register_set("__sysctl_set")
#     registers it during boot, and the constructor then registers the
#     same oid a second time -- which :242 panics on by design.
#
#     This is the first fix here that is NOT a compile error. It is a
#     pure kext-vs-built-in behavioural difference, invisible to every
#     compile-time and link-time check, and it kills the machine roughly
#     26 seconds into boot. Worth remembering as a category: vendoring a
#     kext into the kernel changes which init machinery runs, not just
#     how the code is compiled.
#
#     Fix: drop the manual register/unregister and let the kernel's own
#     linker-set registration do it. The sysctl still exists and still
#     works; only the redundant second registration goes away.
F="$USB_SRC/IOUSBController.cpp"
if LC_ALL=C grep -q '^	sysctl_register_oid ( &sysctl__debug_USB );' "$F"; then
	LC_ALL=C sed -i '' \
		's|^	sysctl_register_oid ( \&sysctl__debug_USB );|	/* removed by tools/vendor_usb_into_xnu.sh: built into the kernel, so\n	 * SYSCTL_PROC already registered this via __sysctl_set; registering\n	 * it again panics (kern_newsysctl.c:242). */|' "$F"
fi
if LC_ALL=C grep -q '^	sysctl_unregister_oid ( &sysctl__debug_USB );' "$F"; then
	LC_ALL=C sed -i '' \
		's|^	sysctl_unregister_oid ( \&sysctl__debug_USB );|	/* removed by tools/vendor_usb_into_xnu.sh -- see the constructor. */|' "$F"
fi
if LC_ALL=C grep -qE '^	sysctl_(un)?register_oid \( &sysctl__debug_USB \);' "$F"; then
	echo "error: IOUSBController.cpp sysctl double-registration fix did not apply" >&2
	exit 1
fi

# 2. linkup.h: `char *period` takes strchr()'s const char* return.
F="$CDC_SRC/linkup.h"
if LC_ALL=C grep -q '	char \*period;' "$F"; then
	LC_ALL=C sed -i '' 's|	char \*period;|	const char *period;|' "$F"
fi
if LC_ALL=C grep -q '	char \*period;' "$F"; then
	echo "error: linkup.h const fix did not apply" >&2
	exit 1
fi
if LC_ALL=C grep -q 'const const' "$F"; then
	echo "error: linkup.h fix produced a duplicate 'const'" >&2
	exit 1
fi

# ---------------------------------------------------------------------
# iokit/conf/files entries.
#
# Inserted rather than appended, immediately after the existing
# iokit/Network block, so the file keeps its family-by-family grouping.
# Every .cpp that exists on disk must be listed or it is copied but never
# compiled -- the same silent no-op sync_iokit_drivers.sh guards against.
# ---------------------------------------------------------------------
MARKER="# --- USB (IOUSBFamily + AppleUSBCDCDriver), vendored by tools/vendor_usb_into_xnu.sh ---"
if ! LC_ALL=C grep -q "^$MARKER$" "$CONF"; then
	{
		echo ""
		echo "$MARKER"
		# Includes BCM2837USBKernelGlue.cpp -- it was copied into
		# $USB_SRC above, so the glob already covers it.
		for f in "$USB_SRC"/*.cpp; do
			printf 'iokit/USB/%s\t\toptional iokitcpp\n' "$(basename "$f")"
		done
		for f in AppleUSBCDC.cpp AppleUSBCDCECMControl.cpp AppleUSBCDCECMData.cpp; do
			printf 'iokit/Drivers/AppleUSBCDC/%s\t\toptional iokitcpp\n' "$f"
		done
		printf 'iokit/Drivers/AppleUSBCDC/MsgTrace.c\t\toptional iokitcpp\n'
	} >> "$CONF"
	echo "+ added USB entries to $CONF"
else
	echo "= $CONF already has the USB block"
fi

# Verify every vendored .cpp/.c really is listed.
missing=()
for f in "$USB_SRC"/*.cpp "$CDC_SRC"/*.cpp "$CDC_SRC"/*.c; do
	[ -e "$f" ] || continue
	rel="${f#$XNU/}"
	LC_ALL=C grep -q "^$rel[[:space:]]" "$CONF" || missing+=("$rel")
done
if [ ${#missing[@]} -ne 0 ]; then
	echo "error: vendored but not listed in $CONF (would never compile):" >&2
	for f in "${missing[@]}"; do
		printf '  %s\toptional iokitcpp\n' "$f" >&2
	done
	exit 1
fi

echo "=== USB vendored into $XNU ==="
echo "  iokit/USB:                    $(ls "$USB_SRC"/*.cpp | wc -l | tr -d ' ') sources"
echo "  iokit/IOKit/usb:              $(ls "$USB_HDR"/*.h | wc -l | tr -d ' ') headers"
echo "  iokit/Drivers/AppleUSBCDC:    $(ls "$CDC_SRC"/*.cpp "$CDC_SRC"/*.c 2>/dev/null | wc -l | tr -d ' ') sources"
