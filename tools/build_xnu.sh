#!/bin/bash
# Builds the real XNU kernel from third_party/xnu (anurodhp/xnu-pi3 fork)
# using the hand-built toolchain in build/xnu-src-build (see build_env.sh
# for why: xcodebuild/xcrun can't run at all under Xcode 12 on this host).
set -ex
cd "$(dirname "$0")/.."
source build/xnu-src-build/build_env.sh
# host_tools/bin/python is a python3 symlink - some of the packaging scripts
# (lldbmacros install) are still #!/usr/bin/env python, which this host doesn't have.
export PATH="$PWD/build/xnu-src-build/host_tools/bin:$PATH"
cd third_party/xnu
# /usr/bin/make is itself an xcrun-resolving stub ("xcrun -find make") under
# DEVELOPER_DIR=Xcode-12 - hits the same CoreChart crash as xcodebuild/bison/m4
# above. Use Homebrew's real GNU Make directly instead.
exec /opt/homebrew/bin/gmake LOGCOLORS=y \
	SDKROOT=iphoneos \
	ARCH_CONFIGS=ARM64 \
	KERNEL_CONFIGS=DEBUG \
	ARCH_STRING_FOR_CURRENT_MACHINE_CONFIG=arm64 \
	MACHINE_CONFIGS=BCM2837 \
	BUILD_WERROR=0 \
	DO_CTFMERGE=0 \
	build \
	"$@"
