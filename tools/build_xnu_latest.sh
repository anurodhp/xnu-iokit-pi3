#!/bin/bash
# Builds XNU from third_party/xnu-latest (real apple-oss-distributions/xnu,
# xnu-12377.121.6, matching this host's actual Xcode/SDK - unlike
# tools/build_xnu.sh's xnu-6153.11.26 tree, this one uses the REAL modern
# xcodebuild (no CoreChart workaround needed - see project memory).
set -ex
cd "$(dirname "$0")/.."
BUILD_ROOT="$PWD/build/xnu-latest-build"

unset DEVELOPER_DIR
export SDKROOT_RESOLVED="$BUILD_ROOT/sdk/MacOSX.sdk"
export CTFCONVERT="$BUILD_ROOT/dtrace/dst/usr/local/bin/ctfconvert"
export CTFMERGE="$BUILD_ROOT/dtrace/dst/usr/local/bin/ctfmerge"

cd third_party/xnu-latest
exec make LOGCOLORS=y \
	SDKROOT=macosx \
	SDKVERSION=26.5 \
	RC_DARWIN_KERNEL_VERSION=25.5.0 \
	ARCH_CONFIGS=ARM64 \
	ARCH_STRING_FOR_CURRENT_MACHINE_CONFIG=arm64 \
	MACHINE_CONFIGS=BCM2837 \
	SUPPORTED_ARM64_MACHINE_CONFIGS="T6000 T6020 T6030 T6031 T6041 T6050 T8101 T8103 T8112 T8122 T8132 T8142 VMAPPLE BCM2837" \
	MACHINE_FLAGS_ARM64_BCM2837="-DARM64_BOARD_CONFIG_BCM2837 -mcpu=cortex-a53" \
	KERNEL_CONFIGS=DEVELOPMENT \
	BUILD_WERROR=0 \
	"$@"
