#!/bin/bash
#
# Generates third_party/_sdk_shim -- the private `<IOKit/network/...>`,
# `<IOKit/storage/...>` and `<IOKit/usb/...>` include tree this project's
# kexts compile against.
#
# Why this exists at all: xnu-7195 ships `iokit/IOKit/*.h` (IOService,
# IOMemoryDescriptor, IOWorkLoop, ...) but NOT the family headers that
# live in separate Apple projects -- IONetworkingFamily, IOStorageFamily
# and IOUSBFamily each install their own headers into
# `/System/Library/Frameworks/Kernel.framework/Headers/IOKit/<family>/`
# on a real macOS build. There is no `make installhdrs` step in this
# project, and Xcode 12's iPhoneOS SDK has no Kernel.framework at all, so
# the include paths those families' own sources use
# (`#include <IOKit/network/IOEthernetController.h>`) would not resolve.
# This tree is a flat symlink farm that makes them resolve straight into
# the pinned third_party/ checkouts -- no copying, so a re-pin of any of
# those repos is picked up automatically.
#
# `compile_flags.txt` puts `-Ithird_party/_sdk_shim` on the include path,
# which is what turns these into `<IOKit/...>` includes.
#
# History note: this tree was originally built by hand (2026-08-07) and
# had no reproducing script for a month, which is exactly the situation
# CLAUDE.md's "every built artifact needs a reproducing script, checked
# in" rule exists to prevent -- a fresh `git clone` +
# `setup_third_party.sh` produced a tree that could not compile a single
# kext, with nothing in the repo saying why. Written 2026-09-08 while
# adding the `IOKit/usb` arm for DAR-243/DAR-244; the network/storage
# arms below reproduce the hand-built tree exactly (verified file-for-
# file and target-for-target against it before replacing it).
#
# Idempotent: safe to re-run; it rebuilds the tree from scratch each time.

set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

SHIM="third_party/_sdk_shim"

for r in IONetworkingFamily IOStorageFamily IOUSBFamily IOPCIFamily IOSerialFamily; do
	if [ ! -d "third_party/$r" ]; then
		echo "error: third_party/$r missing -- run ./setup_third_party.sh first" >&2
		exit 1
	fi
done

# Build into a scratch directory and swap it into place only once every
# header has been verified, rather than deleting the live tree up front.
#
# Two real reasons, both found in review rather than in theory:
#  1. link_headers exits under `set -e` on the first missing header. If
#     the live tree had already been deleted, a bad re-pin would leave NO
#     shim at all, and the next kext compile would bury this script's one
#     clear error message under hundreds of missing-<IOKit/...> errors.
#  2. CLAUDE.md actively encourages running per-library build scripts as
#     parallel background subagents, and tools/build_iousbfamily.sh and
#     tools/build_appleusbcdc.sh both call this script. Two concurrent
#     runs deleting and repopulating one shared tree would pull headers
#     out from under an in-flight compile. A `mv` of a completed tree is
#     as close to atomic as this gets; the loser of a race replaces a
#     complete tree with an identical complete tree.
SHIM_NEW="${SHIM}.new.$$"
rm -rf "$SHIM_NEW"
trap 'rm -rf "$SHIM_NEW"' EXIT
mkdir -p "$SHIM_NEW/IOKit/network" "$SHIM_NEW/IOKit/storage" \
	"$SHIM_NEW/IOKit/usb" "$SHIM_NEW/IOKit/pci" "$SHIM_NEW/IOKit/serial"

# Two hand-written compatibility headers sit at the top of the tree and
# are NOT generated from any upstream source. They used to exist only
# inside the (gitignored) shim tree itself, i.e. nowhere in the repo --
# they now live in compat/sdk_shim/ and are symlinked into place.
for h in mach_version_skew_compat.h verify_only_IOBlockStorageDevice_stub.h; do
	if [ ! -f "compat/sdk_shim/$h" ]; then
		echo "error: compat/sdk_shim/$h missing" >&2
		exit 1
	fi
	ln -sfn "$REPO_ROOT/compat/sdk_shim/$h" "$SHIM_NEW/$h"
done

link_headers() {
	# link_headers <dest subdir under IOKit/> <source dir> <header names...>
	local sub="$1"; shift
	local src="$1"; shift
	local h
	for h in "$@"; do
		if [ ! -f "$src/$h" ]; then
			echo "error: expected header $src/$h not found (wrong pin?)" >&2
			exit 1
		fi
		ln -sfn "$REPO_ROOT/$src/$h" "$SHIM_NEW/IOKit/$sub/$h"
	done
}

# --- IOKit/network -------------------------------------------------------
# IONetworkingFamily keeps every header flat at the repo root. Only the
# subset this project's drivers (SMSC9514Ethernet, and now
# AppleUSBCDCECMData) actually include, plus their transitive includes.
link_headers network third_party/IONetworkingFamily \
	IOBasicOutputQueue.h \
	IOEthernetController.h \
	IOEthernetInterface.h \
	IOEthernetStats.h \
	IOGatedOutputQueue.h \
	IOKernelDebugger.h \
	IOMbufMemoryCursor.h \
	IOMbufQueue.h \
	IONetworkController.h \
	IONetworkControllerPrivate.h \
	IONetworkData.h \
	IONetworkDebug.h \
	IONetworkInterface.h \
	IONetworkMedium.h \
	IONetworkStack.h \
	IONetworkStats.h \
	IONetworkTypesPrivate.h \
	IONetworkUserClient.h \
	IOOutputQueue.h \
	IOPacketQueue.h

# --- IOKit/storage -------------------------------------------------------
# NOTE: the hand-built tree this replaces also carried three DANGLING
# symlinks -- IORequest.h, IORequestsPool.h and
# IOUserBlockStorageDevice_kext.h -- pointing at files that do not exist
# in IOStorageFamily-260.40.5. They are leftovers from the older
# IOStorageFamily-218.1.4 pin that 2026-08-24's manifest audit corrected
# (docs/apple-oss-catalog.md); the re-pin moved the checkout but nobody
# regenerated the by-hand symlink farm on top of it. Nothing includes
# them, so this was harmless, but it is a small worked example of why the
# tree needed a script: a generated tree fails loudly on a bad pin (the
# link_headers existence check below), a hand-built one just rots.
link_headers storage third_party/IOStorageFamily \
	IOAppleLabelScheme.h \
	IOApplePartitionScheme.h \
	IOBlockStorageDevice.h \
	IOBlockStorageDriver.h \
	IOBlockStoragePerfControlExports.h \
	IOFDiskPartitionScheme.h \
	IOFilterScheme.h \
	IOGUIDPartitionScheme.h \
	IOMedia.h \
	IOMediaBSDClient.h \
	IOPartitionScheme.h \
	IOStorage.h \
	IOStorageCardCharacteristics.h \
	IOStorageControllerCharacteristics.h \
	IOStorageDeviceCharacteristics.h \
	IOStorageProtocolCharacteristics.h

# --- IOKit/usb -----------------------------------------------------------
# Added 2026-09-08 (DAR-244). IOUSBFamily's own sources and every client
# of it (AppleUSBCDCDriver in particular) include these as
# <IOKit/usb/...>. Requires the IOUSBFamily-560.4.2 pin, NOT 630.4.5 --
# at 630.4.5 upstream publishes IOUSBController.h/USB.h/IOUSBLog.h/
# IOUSBPriv.h/IOUSBControllerV3.h/IOUSBWorkLoop.h as literal zero-byte
# files (see docs/apple-oss-catalog.md's 2026-09-08 addendum). The
# non-empty check below is a real guard against silently regenerating a
# shim full of empty headers if someone re-pins backwards.
# Linked wholesale rather than as a curated subset (unlike network/
# storage above): IOUSBFamily's Headers/ directory IS the installed
# <IOKit/usb/> directory upstream, its headers include each other
# through that same path, and this project is porting the family itself
# rather than just consuming a handful of its headers.
link_headers usb third_party/IOUSBFamily/IOUSBFamily/Headers \
	$(cd "$REPO_ROOT/third_party/IOUSBFamily/IOUSBFamily/Headers" && ls *.h)

# IOUSBFamily splits its user-client headers into a sibling project
# directory, but they install into the same <IOKit/usb/> namespace and
# the family's own sources include them both ways:
# IOUSBControllerUserClient.h says <IOKit/usb/IOUSBUserClient.h>, while
# IOUSBPipe.cpp/IOUSBPipeV2.cpp say "IOUSBInterfaceUserClient.h". Linked
# here so the <> form resolves; the "" form needs
# -Ithird_party/IOUSBFamily/IOUSBUserClient/Headers on the command line
# (tools/build_iousbfamily.sh adds it).
link_headers usb third_party/IOUSBFamily/IOUSBUserClient/Headers \
	IOUSBDeviceUserClient.h \
	IOUSBInterfaceUserClient.h \
	IOUSBInterfaceUserClientV3.h \
	IOUSBUserClient.h

# --- IOKit/pci -----------------------------------------------------------
# Header-only, and only because IOUSBFamily needs the declarations to
# parse: IOUSBControllerV3.h has an `IOPCIDevice *_device` member and
# IOUSBController.cpp references IOPCIDevice in 8 places. BCM2837 has no
# PCI bus, so none of that code can execute here -- but it does have to
# compile. IOPCIFamily already lays its headers out as IOKit/pci/*.h
# upstream, so this arm is a straight pass-through.
link_headers pci third_party/IOPCIFamily/IOKit/pci \
	IOAGPDevice.h \
	IOPCIBridge.h \
	IOPCIConfigurator.h \
	IOPCIDevice.h \
	IOPCIFamilyDefinitions.h \
	IOPCIPrivate.h

# --- IOKit/serial --------------------------------------------------------
# Header-only, and only for AppleUSBCDC.cpp -- the CDC master driver both
# AppleUSBCDCECMControl and AppleUSBCDCECMData depend on, which includes
# these for its ACM (modem/serial) sibling path. This project has no
# serial-port devices; nothing here can execute. IOSerialFamily keeps
# everything flat inside IOSerialFamily.kmodproj/.
link_headers serial third_party/IOSerialFamily/IOSerialFamily.kmodproj \
	IOModemSerialStreamSync.h \
	IORS232SerialStreamSync.h \
	IOSerialDriverSync.h \
	IOSerialKeys.h \
	IOSerialStreamSync.h \
	ioss.h

# Zero-byte guard, over EVERY arm rather than just IOKit/usb.
#
# The failure mode it exists for was found in IOUSBFamily (tag 630.4.5
# publishes ten .cpp files and eight headers as literal zero-byte files
# upstream -- see docs/apple-oss-catalog.md's 2026-09-08 addendum), but
# nothing makes that unique to IOUSBFamily, and an empty header is far
# worse than a missing one: `-s` is the only check that catches it, since
# the symlink resolves, the file exists, and the #include succeeds
# silently, leaving whatever the header should have declared simply
# absent. The existence check in link_headers cannot see it at all.
#
# Scoped to headers only. Directories are checked, not assumed, so adding
# an arm above without adding it here fails loudly.
empty=0
for arm in network storage usb pci serial; do
	if [ ! -d "$SHIM_NEW/IOKit/$arm" ]; then
		echo "error: expected arm IOKit/$arm was never created" >&2
		exit 1
	fi
	for h in "$SHIM_NEW/IOKit/$arm"/*.h; do
		if [ ! -s "$h" ]; then
			echo "error: IOKit/$arm/$(basename "$h") resolves to a ZERO-BYTE file" >&2
			empty=1
		fi
	done
done
if [ "$empty" = "1" ]; then
	cat >&2 <<'EOF'
error: at least one pinned family publishes empty source at its current
       tag. The known case is IOUSBFamily-630.4.5 -- re-pin to
       IOUSBFamily-560.4.2. See docs/apple-oss-catalog.md's 2026-09-08
       addendum for the full list and how it was verified upstream.
EOF
	exit 1
fi

# Everything verified: swap the completed tree into place. Clearing the
# trap first so a failure after this point cannot delete the live tree.
trap - EXIT
rm -rf "$SHIM"
mv "$SHIM_NEW" "$SHIM"

echo "=== third_party/_sdk_shim regenerated ==="
echo "  IOKit/network: $(ls "$SHIM/IOKit/network" | wc -l | tr -d ' ') headers"
echo "  IOKit/storage: $(ls "$SHIM/IOKit/storage" | wc -l | tr -d ' ') headers"
echo "  IOKit/usb:     $(ls "$SHIM/IOKit/usb" | wc -l | tr -d ' ') headers"
echo "  IOKit/pci:     $(ls "$SHIM/IOKit/pci" | wc -l | tr -d ' ') headers"
echo "  IOKit/serial:  $(ls "$SHIM/IOKit/serial" | wc -l | tr -d ' ') headers"
