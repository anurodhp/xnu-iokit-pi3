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
set -ex
cd "$(dirname "$0")/.."
BUILD_ROOT="$PWD/build/xnu-latest-build"

unset DEVELOPER_DIR
export SDKROOT_RESOLVED="$BUILD_ROOT/sdk-7195-ios/iPhoneOS.sdk"
export CTFCONVERT="$BUILD_ROOT/dtrace/dst/usr/local/bin/ctfconvert"
export CTFMERGE="$BUILD_ROOT/dtrace/dst/usr/local/bin/ctfmerge"

cd third_party/xnu-7195
exec make LOGCOLORS=y \
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
