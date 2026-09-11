#!/bin/bash
# Builds third_party/qemu-zhuowei, the QEMU fork runvm.sh boots against.
#
# This existed only as shell history until DAR-282 needed a second real
# change to that fork (the first was the DWC2 host-controller backport for
# Phase 6a). Every built artifact in this repo needs a reproducing script
# checked in -- a hand-run `./configure && make` in a gitignored directory
# is exactly the "only exists in an agent's transcript" case that rule is
# about, and it is worse than usual here: the tree is configured IN PLACE,
# so a fresh clone has no config-host.mak at all and `make` fails with a
# message that does not mention configure.
#
# The configure line is not invented. It is the one recorded in the
# already-configured tree's own config.status / config.log header, and is
# reproduced verbatim below so a fresh clone gets the same binary as the
# one every prior boot test in this project ran against:
#
#   --target-list=aarch64-softmmu   only board this project boots
#   --disable-docs                  needs sphinx, produces nothing we read
#   --python=python3                this QEMU vintage still probes `python`
#   --disable-git-update            never touch the fork's own submodules
#   --extra-{c,ld}flags=.../dtc     Homebrew libfdt; QEMU's bundled dtc
#                                   submodule is not checked out here
#   --disable-{gnutls,nettle,gcrypt,vnc-sasl,vnc,curses,sdl,gtk,libssh2,
#     smartcard,libusb}
#                                   no display or crypto backend is used
#                                   (-display none, -serial stdio), and
#                                   each of these is a real build break or
#                                   a real dependency install on a current
#                                   macOS host
#
# Note --disable-libusb specifically: this project's USB work is entirely
# emulated (usb-net/usb-hub behind the backported DWC2 model), never USB
# passthrough, so host libusb is genuinely unused rather than merely
# inconvenient.
#
# Incremental by default -- re-running after editing one file relinks in
# seconds. Pass --reconfigure to force configure to run again (needed after
# changing the options above, or after a `git clean`).
set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
QEMU="$REPO_ROOT/third_party/qemu-zhuowei"

if [ ! -d "$QEMU" ]; then
	echo "error: $QEMU not found - run ./setup_third_party.sh" >&2
	exit 1
fi

DTC_PREFIX="${DTC_PREFIX:-/opt/homebrew/opt/dtc}"
if [ ! -d "$DTC_PREFIX/include" ]; then
	echo "error: libfdt headers not found at $DTC_PREFIX/include" >&2
	echo "       install with: brew install dtc" >&2
	echo "       (or set DTC_PREFIX to an existing prefix)" >&2
	exit 1
fi

RECONFIGURE=0
if [ "${1:-}" = "--reconfigure" ]; then
	RECONFIGURE=1
	shift
fi

cd "$QEMU"

if [ ! -f config-host.mak ] || [ "$RECONFIGURE" -eq 1 ]; then
	echo "=== configuring qemu-zhuowei ==="
	./configure \
		--target-list=aarch64-softmmu \
		--disable-docs \
		--python=python3 \
		--disable-git-update \
		--extra-cflags="-I$DTC_PREFIX/include" \
		--extra-ldflags="-L$DTC_PREFIX/lib" \
		--disable-gnutls \
		--disable-nettle \
		--disable-gcrypt \
		--disable-vnc-sasl \
		--disable-vnc \
		--disable-curses \
		--disable-sdl \
		--disable-gtk \
		--disable-libssh2 \
		--disable-smartcard \
		--disable-libusb
else
	echo "= already configured (pass --reconfigure to redo)"
fi

make -j"$(sysctl -n hw.ncpu)" "$@"

BIN="$QEMU/aarch64-softmmu/qemu-system-aarch64"
if [ ! -x "$BIN" ]; then
	echo "error: build finished but $BIN is missing" >&2
	exit 1
fi
echo "=== built ${BIN#$REPO_ROOT/} ==="
