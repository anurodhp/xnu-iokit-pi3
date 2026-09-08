#!/bin/bash
#
# Generates generated/libkern/version.h using xnu-7195's OWN generator
# (config/newvers.pl) against its OWN config/MasterVersion, exactly the
# way third_party/xnu-7195/libkern/libkern/Makefile does during a real
# kernel build:
#
#     version.h: version.h.template $(SRCROOT)/config/MasterVersion
#             install $< $@
#             $(NEWVERS) $@          # NEWVERS = $(SRCROOT)/config/newvers.pl
#
# WHY IT IS NEEDED HERE: libkern/version.h does not exist in the xnu
# source tree -- only libkern/version.h.template does, and the real value
# is stamped in at build time. Kernel sources that `#include
# <libkern/version.h>` therefore compile fine inside a kernel build and
# fail outside one. tools/build_kexts.sh and tools/build_iousbfamily.sh
# compile outside one. Nothing in this project needed the header until
# IOUSBFamily arrived (DAR-244): IOUSBController.cpp, IOUSBCommand.cpp,
# IOUSBControllerV3.cpp and IOUSBInterface.cpp all include it, to compare
# `version_major` against the OS release at runtime.
#
# Deliberately NOT a hand-written stand-in with plausible-looking
# constants. The values here are real: VERSION_MAJOR/MINOR/REVISION come
# out of xnu-7195's MasterVersion (20.3.0 -- Darwin 20.3.0/macOS 11.2,
# the version this whole project is pinned to), so a `version_major >= N`
# test compiled by this script behaves the same as one compiled into the
# kernel. A fabricated header would compile just as cleanly and be wrong.
#
# SRCROOT/OBJROOT are the two environment variables newvers.pl requires;
# it uses OBJROOT only to fill in ###KERNEL_BUILD_OBJROOT###, which is a
# provenance string, not a semantic value.
#
# Idempotent apart from the embedded build date/objroot string.

set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

XNU="$REPO_ROOT/third_party/xnu-7195"
TEMPLATE="$XNU/libkern/libkern/version.h.template"
NEWVERS="$XNU/config/newvers.pl"
OUT_DIR="generated/libkern"
OUT="$OUT_DIR/version.h"

for f in "$TEMPLATE" "$NEWVERS" "$XNU/config/MasterVersion"; do
	if [ ! -f "$f" ]; then
		echo "error: $f not found - run ./setup_third_party.sh" >&2
		exit 1
	fi
done

mkdir -p "$OUT_DIR"
cp "$TEMPLATE" "$OUT"

SRCROOT="$XNU" \
OBJROOT="$REPO_ROOT/build/xnu-src-build" \
CURRENT_KERNEL_CONFIG_LC="development" \
	perl "$NEWVERS" "$OUT" > /dev/null

# newvers.pl substitutes in place and is silent on a no-op, so check that
# every placeholder really got replaced rather than trusting exit status.
if grep -q '###' "$OUT"; then
	echo "error: $OUT still contains unsubstituted ### placeholders:" >&2
	grep -n '###' "$OUT" >&2
	exit 1
fi

echo "=== generated $OUT ==="
grep -E '#define (VERSION_MAJOR|VERSION_MINOR|VERSION_REVISION|OSRELEASE)' "$OUT"
