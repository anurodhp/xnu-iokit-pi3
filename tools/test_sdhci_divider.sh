#!/bin/bash
#
# Builds and runs the host-side exhaustive test for BCM2837SDHCI's SD clock
# divider math (DAR-103). See tools/test_sdhci_divider.c's header for what
# it checks and why it cannot be a QEMU boot test instead (QEMU's SDHCI
# model ignores the divider field entirely, so emulation can never catch a
# wrong divisor).
#
# Runs on the host, natively, with whatever cc is on PATH - it compiles one
# freestanding-arithmetic header and no kernel/IOKit code at all, so it
# needs neither Xcode 12 nor the iPhoneOS SDK that every other build script
# here requires.
#
#   tools/test_sdhci_divider.sh
#
# Exits nonzero if any check fails.

set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

OUT_DIR="${1:-build/tests}"
mkdir -p "$OUT_DIR"
BIN="$OUT_DIR/test_sdhci_divider"

cc -std=c99 -Wall -Wextra -Werror -O2 \
	-o "$BIN" tools/test_sdhci_divider.c

"$BIN"
