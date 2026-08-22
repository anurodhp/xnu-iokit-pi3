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
#   - dtc-AppleDeviceTree, u-boot, Csu: small local-only fixes on top of a
#     third-party (not personal-fork) upstream -- kept as patch files in
#     third_party-patches/ and applied after cloning upstream, rather than
#     standing up a whole personal fork for a single commit each. Csu.patch
#     (3 commits) closes a real reproducibility gap: the first two (real
#     `environ`/`__progname`/NXArgc/NXArgv globals, Phase 4.6) had been
#     committed only in this checkout's own local git history with no
#     patch file and no personal fork remote -- silently lost on a fresh
#     `--force` re-clone. Bundled here with the third (2026-08-22,
#     `__DATA,__mod_init_func` constructor support for real ifconfig --
#     see tools/userland_staging/build_ifconfig.sh) rather than left
#     unresolved further.
#   - hfs, IOStorageFamily, IOUSBFamily, IONetworkingFamily, msdosfs: unmodified Apple
#     open-source reference sources, pinned to an exact tag, no patching.
#   - dyld/Libc/libpthread/.../Csu (Phase 4 libSystem+userland stack) and
#     bash/shell_cmds/.../misc_cmds (Phase 4 real commands): unmodified
#     Apple sources, pinned to the tags apple-oss-distributions'
#     `distribution-macOS` repo actually shipped together for macOS 11.2
#     (tag `macos-112` there -- its pinned `xnu` submodule commit
#     (e890dd3c) matches `xnu-7195.81.3^{commit}` exactly, i.e. this
#     project's real xnu-7195 checkout's exact Darwin 20.3.0
#     (config/MasterVersion) -- confirmed, not guessed by date proximity).
#     `distribution-macOS` is a real cross-project version manifest, one
#     `git ls-tree macos-112 -- <name>` away from an exact matching tag for
#     any other apple-oss-distributions repo Phase 4 ever needs -- prior
#     project notes assumed no such manifest existed; it does.
#   - gnudiff/less/libarchive (Tier B command sweep, 2026-08-20): same
#     `macos-112` manifest method, one wrinkle each --
#     `git ls-tree macos-112 -- diffutils` has NO entry at all (real gap,
#     not a miss); the manifest calls real GNU diff by its actual
#     apple-oss-distributions project name, `gnudiff`
#     (commit 6c956e21239393eee016d92c441c3f61d8bdaa96 == tag
#     `gnudiff-25.40.1`, confirmed via `git ls-remote --tags`, not
#     guessed). `less` and `libarchive` matched their own project names
#     directly (commits 9c1581935ce26141f6118d3f9d136b7347a82122 ==
#     `less-34`, 70fe9c400e7f10398c18ccc883b706d9694dd14b ==
#     `libarchive-83.40.4`).
#   - CommonCrypto (Phase 4.6 item 6, 2026-08-21): same `macos-112`
#     manifest method -- `git ls-tree macos-112 -- CommonCrypto` ==
#     commit 0e9014afa4389c0bf127c79aaf3bb9204b29b9cd == tag
#     `CommonCrypto-60178.40.2` (confirmed via `git ls-remote --tags`).
#     Real Apple source, but only its public/private *headers* and thin
#     CC_*/CCDigest* glue (`lib/CommonDigest.c`) are usable here -- that
#     glue itself `#include`s `<corecrypto/ccmd5.h>` etc and calls
#     `ccdigest_init/update/final`, and the actual corecrypto digest
#     compression cores are NOT present in this checkout (confirmed:
#     `third_party/xnu-7195/EXTERNAL_HEADERS/corecrypto/` has only
#     `cc.h`/`cc_config.h`/`ccmode_impl.h`/`ccpad.h` -- headers, no
#     `ccdigest.c`/`ccmd5.c`/`ccsha2.c` implementation -- and this
#     CommonCrypto checkout doesn't vendor corecrypto either), so
#     `lib/CommonDigest.c` cannot actually link/run standalone here.
#     Used for its real, unmodified `include/CommonDigest.h` +
#     `include/Private/CommonDigestSPI.h` headers (CC_MD5_CTX layout,
#     CCDigestAlg/CCDigestCtx/CCDigestRef, real function prototypes);
#     the digest math itself is a from-scratch fallback -- see
#     `tools/userland_staging/md5_commoncrypto_shim.c`'s header comment.
#   - network_cmds (Phase 6b `ifconfig`, 2026-08-22): same `macos-112`
#     manifest method -- `git ls-tree macos-112 -- network_cmds` ==
#     commit 80f2c23cabef8839f8ec50611cf43d4033fc8ed9 == tag
#     `network_cmds-606.40.2` (confirmed via `git ls-remote --tags`).
#     Real Apple ifconfig.tproj source; only ifconfig.c/af_inet.c/
#     af_link.c/ifmedia.c/ifclone.c are actually compiled -- see
#     `tools/userland_staging/build_ifconfig.sh`'s own header comment for
#     which real address-family/clone plugins were scoped out and why.
#   - cron (Phase 5.5 item 3, 2026-08-22): same `macos-112` manifest
#     method -- `git ls-tree macos-112 -- cron` == commit
#     43ca0402cc51c9c81171c9ee1b92b28ed6148219 == tag `cron-40` (confirmed
#     via `git ls-remote --tags`). Real, unmodified Apple `cron`/
#     `crontab` source. `ntp` was investigated the same way and
#     deliberately NOT pinned: `git ls-tree macos-112 -- ntp` has no entry
#     at all (checked the full 232-entry manifest, not just this one
#     lookup) -- real macOS 11.2 shipped no ntpd source at all, having
#     already moved to the closed-source `timed` by Big Sur. The
#     `apple-oss-distributions/ntp` repo still exists (newest tag
#     `ntp-139`) but pinning it would mean an unverifiable vintage
#     mismatch with no manifest to confirm against, unlike every other
#     pin here -- see `docs/daemon-roadmap.md` for the full writeup.
#     Verified (2026-08-22): real macOS has not used ntpd since High
#     Sierra (10.13, 2017) either, replaced by the closed-source `timed`
#     -- so this isn't just a this-project gap, ntpd is dead upstream too.
#     ntp sync, if wanted later, should come from FreeBSD's actively-
#     maintained `contrib/ntp` (reference NTP Project code, genuinely
#     portable, NTP license) instead -- not yet pinned, tracked in
#     `docs/daemon-roadmap.md`.
#   - libutil (Phase 5.5 item 3, 2026-08-22): `cron`'s pidfile handling
#     (`pidfile_open`/`pidfile_write`/`pidfile_close`, `<libutil.h>`)
#     needed a real source this project hadn't pulled yet. Same
#     `macos-112` manifest method -- `git ls-tree macos-112 -- libutil`
#     == commit 82b4f3ed687a5226c3404dc290c84f1897874c08 == tag
#     `libutil-58.40.2` (confirmed via `git ls-remote --tags`).
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
IONetworkingFamily|https://github.com/apple-oss-distributions/IONetworkingFamily.git|IONetworkingFamily-151.40.1|
msdosfs|https://github.com/apple-oss-distributions/msdosfs.git|msdosfs-229.1.4|
dyld|https://github.com/apple-oss-distributions/dyld.git|dyld-832.7.3|
Libc|https://github.com/apple-oss-distributions/Libc.git|Libc-1439.40.11|
libpthread|https://github.com/apple-oss-distributions/libpthread.git|libpthread-454.80.2|
libplatform|https://github.com/apple-oss-distributions/libplatform.git|libplatform-254.80.2|
libdispatch|https://github.com/apple-oss-distributions/libdispatch.git|libdispatch-1271.40.12|
libclosure|https://github.com/apple-oss-distributions/libclosure.git|libclosure-78|
libmalloc|https://github.com/apple-oss-distributions/libmalloc.git|libmalloc-317.40.8|
libunwind|https://github.com/apple-oss-distributions/libunwind.git|libunwind-200.10|
Libinfo|https://github.com/apple-oss-distributions/Libinfo.git|Libinfo-542.40.3|
removefile|https://github.com/apple-oss-distributions/removefile.git|removefile-49.40.3|
copyfile|https://github.com/apple-oss-distributions/copyfile.git|copyfile-173.40.2|
Csu|https://github.com/apple-oss-distributions/Csu.git|Csu-88|Csu.patch
bash|https://github.com/apple-oss-distributions/bash.git|bash-123.40.1|bash.patch
shell_cmds|https://github.com/apple-oss-distributions/shell_cmds.git|shell_cmds-216.60.1|
file_cmds|https://github.com/apple-oss-distributions/file_cmds.git|file_cmds-321.40.3|
text_cmds|https://github.com/apple-oss-distributions/text_cmds.git|text_cmds-106|
adv_cmds|https://github.com/apple-oss-distributions/adv_cmds.git|adv_cmds-176|
system_cmds|https://github.com/apple-oss-distributions/system_cmds.git|system_cmds-880.60.2|
basic_cmds|https://github.com/apple-oss-distributions/basic_cmds.git|basic_cmds-55|
misc_cmds|https://github.com/apple-oss-distributions/misc_cmds.git|misc_cmds-34|
gnudiff|https://github.com/apple-oss-distributions/gnudiff.git|gnudiff-25.40.1|
less|https://github.com/apple-oss-distributions/less.git|less-34|
libarchive|https://github.com/apple-oss-distributions/libarchive.git|libarchive-83.40.4|
CommonCrypto|https://github.com/apple-oss-distributions/CommonCrypto.git|CommonCrypto-60178.40.2|
network_cmds|https://github.com/apple-oss-distributions/network_cmds.git|network_cmds-606.40.2|
cron|https://github.com/apple-oss-distributions/cron.git|cron-40|cron.patch
libutil|https://github.com/apple-oss-distributions/libutil.git|libutil-58.40.2|
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
