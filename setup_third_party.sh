#!/bin/bash
# Clones every external repo this project's build/boot needs into
# third_party/ (gitignored -- see .gitignore's note that each has its own
# git history/repo, not vendored into this repo directly). Run this once on
# a fresh checkout, or with --force to wipe and re-clone everything.
#
# Where each repo's own local changes live:
#   - xnu-7195, xnu-latest, qemu-zhuowei: this project's own personal forks
#     (anurodhp/*) carry full commit history of the port work -- cloned
#     directly from there, no patching needed after clone.
#   - dtc-AppleDeviceTree, u-boot: small local-only fixes on top of a
#     third-party (not personal-fork) upstream -- kept as patch files in
#     third_party-patches/ and applied after cloning upstream, rather than
#     standing up a whole personal fork for a single commit each.
#   - hfs, IOStorageFamily, IOUSBFamily, msdosfs: unmodified Apple
#     open-source reference sources, pinned to an exact tag, no patching.
#
# Usage: ./setup_third_party.sh [--force]
set -euo pipefail
cd "$(dirname "$0")"

FORCE=0
if [ "${1:-}" = "--force" ]; then
    FORCE=1
fi

mkdir -p third_party

# name | clone_url | ref (branch or tag) | patch file (optional, relative to third_party-patches/)
REPOS='
xnu-7195|git@github.com:anurodhp/xnu-pi3-macos11.git|bcm2837-bringup|
xnu-latest|git@github.com:anurodhp/xnu-pi3-latest.git|main|
qemu-zhuowei|git@github.com:anurodhp/qemu-xnu-arm.git|master|
dtc-AppleDeviceTree|https://github.com/darwin-on-arm/dtc-AppleDeviceTree.git|master|dtc-AppleDeviceTree.patch
u-boot|https://github.com/FlorentRevest/u-boot.git|master|u-boot.patch
hfs|https://github.com/apple-oss-distributions/hfs.git|hfs-556.100.11|
IOStorageFamily|https://github.com/apple-oss-distributions/IOStorageFamily.git|IOStorageFamily-218.1.4|
IOUSBFamily|https://github.com/apple-oss-distributions/IOUSBFamily.git|IOUSBFamily-630.4.5|
msdosfs|https://github.com/apple-oss-distributions/msdosfs.git|msdosfs-229.1.4|
'

echo "$REPOS" | while IFS='|' read -r name url ref patch; do
    [ -z "$name" ] && continue
    dest="third_party/$name"

    if [ -d "$dest" ]; then
        if [ "$FORCE" = "1" ]; then
            echo "+ removing existing $dest (--force)"
            rm -rf "$dest"
        else
            echo "= $dest already exists, skipping (use --force to re-clone)"
            continue
        fi
    fi

    echo "+ cloning $name @ $ref from $url"
    git clone --branch "$ref" "$url" "$dest"

    if [ -n "$patch" ]; then
        echo "+ applying third_party-patches/$patch to $name"
        git -C "$dest" am "../../third_party-patches/$patch"
    fi
done

echo "done -- third_party/ populated. See docs/roadmap.md and README.md for what to build next."
