#!/bin/bash
# Builds XNU from third_party/xnu-7195 (real apple-oss-distributions/xnu,
# xnu-7195.81.3 - same version as the already-working precompiled kernel,
# and the newest tag with NO TrustCache/CoreEntitlements dependency and
# real first-party BCM2837 board support already in the public source.
# Uses the real modern Xcode/SDK (no CoreChart workaround needed).
#
# SDKROOT=iphoneos, not macosx: BCM2837 is only ever built as an embedded
# target upstream (matches the old xnu-6153 tree's tools/build_xnu.sh too) -
# mixing it with SDKROOT=macosx leaves CONFIG_EMBEDDED (from ARCH_CONFIGS)
# and XNU_TARGET_OS_OSX (from PLATFORM=MacOSX) disagreeing with each other,
# which desyncs struct layouts like vm_map's vmmap_high_start field between
# translation units. See project memory for the full diagnosis.
#
# Two hand-steps that used to bracket this script are now part of it, because
# both failed silently and both cost a full QEMU boot cycle to notice:
#   - iokit/<Driver>/*.cpp are plain copies of the files the XNU tree
#     actually compiles, so an edit here would otherwise build the stale
#     copy. tools/sync_iokit_drivers.sh copies them and checks each is
#     listed in iokit/conf/files.
#   - the kernel lands deep in xnu's BUILD/ tree, while runvm.sh reads
#     build/xnu-latest-build/out/. That copy was documented in CLAUDE.md as
#     something no script did; now it does, so a "rebuilt" kernel cannot
#     silently be the previous one.
#   - tools/vendor_usb_into_xnu.sh's own header comment already claimed
#     this script calls it; it didn't (DAR-277). Now it does, matching
#     sync_iokit_drivers.sh's pattern: idempotent, so calling it every
#     build costs nothing when the vendored copies are already committed
#     and up to date, and it re-vendors instead of silently building a
#     stale copy the next time IOUSBFamily/AppleUSBCDCDriver's pins move.
set -ex
cd "$(dirname "$0")/.."
REPO_ROOT="$PWD"
# Physical paths, not logical ones. Inside a git worktree, third_party/
# (and build/) are symlinks into the main checkout; `cd third_party/
# xnu-7195` through the symlink makes make's SRCROOT the WORKTREE path,
# which then gets baked into BUILD/obj's .cpd dependency files of the one
# shared xnu tree -- and the next build after that worktree is deleted
# fails with "No rule to make target .../.claude/worktrees/...". Resolving
# with cd -P gives every generated path the real location instead.
if [ ! -d build ]; then
	echo "error: $REPO_ROOT/build not found. It must already hold" >&2
	echo "       xnu-latest-build/sdk-7195-ios (no script in this repo creates" >&2
	echo "       that SDK copy). In a git worktree, symlink build/ to the main" >&2
	echo "       checkout's build/, as third_party/ already is." >&2
	exit 1
fi
BUILD_ROOT="$(cd -P build && pwd)/xnu-latest-build"

"$REPO_ROOT/tools/sync_iokit_drivers.sh"
"$REPO_ROOT/tools/vendor_usb_into_xnu.sh"
"$REPO_ROOT/tools/vendor_iographics_into_xnu.sh"
"$REPO_ROOT/tools/vendor_iohid_into_xnu.sh"

unset DEVELOPER_DIR
export SDKROOT_RESOLVED="$BUILD_ROOT/sdk-7195-ios/iPhoneOS.sdk"
export CTFCONVERT="$BUILD_ROOT/dtrace/dst/usr/local/bin/ctfconvert"
export CTFMERGE="$BUILD_ROOT/dtrace/dst/usr/local/bin/ctfmerge"

cd -P third_party/xnu-7195
make LOGCOLORS=y \
	SDKROOT=iphoneos \
	PLATFORM=iPhoneOS \
	PLATFORMPATH=/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform \
	SDKVERSION=26.5 \
	RC_DARWIN_KERNEL_VERSION=22.3.0 \
	ARCH_CONFIGS=ARM64 \
	ARCH_STRING_FOR_CURRENT_MACHINE_CONFIG=arm64 \
	MACHINE_CONFIGS=BCM2837 \
	KERNEL_CONFIGS=DEVELOPMENT \
	BUILD_WERROR=0 \
	"$@"

# Publish the kernel where runvm.sh / tools/boot_test.py look for it. Skipped
# for non-building targets (clean, etc.), which leave no output behind.
BUILT="$REPO_ROOT/third_party/xnu-7195/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837.unstripped"
PUBLISHED="$REPO_ROOT/build/xnu-latest-build/out/kernel.development.bcm2837-7195.unstripped"
if [ -f "$BUILT" ]; then
	mkdir -p "$(dirname "$PUBLISHED")"
	cp "$BUILT" "$PUBLISHED"
	echo "published kernel -> ${PUBLISHED#$REPO_ROOT/}"
fi
