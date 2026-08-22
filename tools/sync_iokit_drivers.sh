#!/bin/bash
# Copies this repo's IOKit driver sources (iokit/<Driver>/*.cpp|*.h) into the
# XNU tree that actually compiles them
# (third_party/xnu-7195/iokit/Drivers/BCM2837/), and verifies every .cpp is
# listed in iokit/conf/files so it really gets built.
#
# Why this exists: those two locations were plain, unlinked copies kept in
# sync by hand. Editing iokit/BCM2837SDHCI/BCM2837SDHCI.cpp and then running
# tools/build_xnu_7195.sh would happily build the *stale* copy and produce a
# kernel with none of the edit in it - a silent no-op that looks exactly like
# "my fix didn't work", which is the worst possible failure mode in a project
# where each verification cycle costs a full QEMU boot. Same class of gap as
# devicetrees/Makefile invoking the wrong dtc (see that file's header).
#
# Run directly, or let tools/build_xnu_7195.sh call it (it does, first thing).
#
# It refuses to overwrite a destination file that has been edited *in the XNU
# tree* without being committed there, because a blind one-way copy would
# silently destroy such an edit. That is not hypothetical either: the very
# first run of this script found iokit/Drivers/BCM2837/BCM2837LocalIntc.cpp
# holding a 2026-08-13 adversarial-review correction (the "backlog item 3
# CORRECTED" comment) that had never been back-ported to iokit/, and
# clobbered it with the older, known-wrong text. Copying blind in the other
# direction is just as bad; the only safe move is to stop and let a human
# say which side is right.
set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

DEST="third_party/xnu-7195/iokit/Drivers/BCM2837"
CONF="third_party/xnu-7195/iokit/conf/files"

if [ ! -d "$DEST" ]; then
	echo "error: $DEST not found - run ./setup_third_party.sh" >&2
	exit 1
fi

# Sources deliberately NOT compiled into the kernel.
#
# Both SMSC9514Ethernet and SMSC9514USBTransport used to be listed here.
# SMSC9514Ethernet subclasses IOEthernetController, so compiling it in
# needed IONetworkingFamily vendored into the kernel build first
# (third_party/xnu-7195/iokit/Network/, iokit/conf/files) - that landed, so
# the reason this was excluded no longer holds. SMSC9514USBTransport never
# needed IONetworkingFamily at all (deliberately avoids including
# SMSC9514Ethernet.h - see its own header) and landed earlier, once
# BCM2837DWC2::start() needed its read-only probe() against real silicon.
NOT_BUILT=()

copied=0
missing_from_conf=()
diverged=()

for src in iokit/*/*.cpp iokit/*/*.h; do
	[ -e "$src" ] || continue
	base="$(basename "$src")"
	dst="$DEST/$base"

	# Skip NOT_BUILT sources and their headers entirely - don't copy them
	# either. The XNU tree should contain exactly what it compiles;
	# shipping sources it ignores just invites someone to edit the copy.
	skip=0
	for nb in "${NOT_BUILT[@]}"; do
		if [ "$base" = "$nb" ] || [ "$base" = "${nb%.cpp}.h" ]; then
			skip=1
		fi
	done
	if [ "$skip" -eq 1 ]; then
		continue
	fi

	if ! cmp -s "$src" "$dst"; then
		# Refuse to clobber an uncommitted edit made on the XNU-tree side.
		if [ -e "$dst" ] && ! git -C third_party/xnu-7195 diff --quiet -- \
			"iokit/Drivers/BCM2837/$base" 2>/dev/null; then
			diverged+=("$base")
		else
			cp "$src" "$dst"
			echo "  synced $src -> $dst"
			copied=$((copied + 1))
		fi
	fi

	case "$base" in
	*.cpp)
		if ! grep -q "iokit/Drivers/BCM2837/$base" "$CONF"; then
			missing_from_conf+=("$base")
		fi
		;;
	esac
done

if [ ${#diverged[@]} -ne 0 ]; then
	echo "error: these files differ from iokit/ AND have uncommitted edits in" >&2
	echo "       the XNU tree, so the XNU-tree copy may be the newer one." >&2
	echo "       Refusing to overwrite. Compare and pick a side, then re-run:" >&2
	for f in "${diverged[@]}"; do
		echo "         git -C third_party/xnu-7195 diff -- iokit/Drivers/BCM2837/$f" >&2
	done
	exit 1
fi

if [ ${#missing_from_conf[@]} -ne 0 ]; then
	echo "error: these sources are not listed in $CONF, so they would be" >&2
	echo "       copied but never compiled. Add a line for each (or add them" >&2
	echo "       to NOT_BUILT in this script if that is intentional):" >&2
	for f in "${missing_from_conf[@]}"; do
		printf 'iokit/Drivers/BCM2837/%s\toptional iokitcpp\n' "$f" >&2
	done
	exit 1
fi

if [ "$copied" -eq 0 ]; then
	echo "iokit drivers already in sync"
else
	echo "iokit drivers: $copied file(s) synced"
fi
