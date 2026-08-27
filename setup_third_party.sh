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
#     CORRECTED 2026-08-24: a full `distribution-macOS` `macos-112` manifest
#     audit (docs/apple-oss-catalog.md) found IOStorageFamily and msdosfs
#     had actually been pinned to tags OLDER than what macOS 11.2/Darwin
#     20.3 really shipped (`IOStorageFamily-218.1.4`/`msdosfs-229.1.4`,
#     vs. the manifest's real `IOStorageFamily-260.40.5`/`msdosfs-435.60.1`)
#     -- a real vintage mismatch on this project's own boot/root-filesystem
#     drivers, not previously caught despite the header comment above's
#     "pinned to an exact tag" claim. Fixed by re-pinning to the manifest-
#     exact tags and re-vendoring `third_party/xnu-7195`'s copies
#     (`iokit/Storage/*.cpp`, `iokit/IOKit/storage/*.h`, `bsd/msdosfs/*`)
#     via a real 3-way merge (`git merge-file`, base=old pristine tag,
#     local=this project's own in-tree fixes, other=new pristine tag) so
#     the project's own local fixes (IOMediaBSDClient.cpp's lazy-singleton
#     CAS fix for backlog item 12; msdosfs_vfsops.c's root-mount
#     `copyin(NULL)` fix + `VFS_TBLCANMOUNTROOT`; msdosfs_vnops.c's
#     DEBUG-gated diagnostics) survived the version bump intact, verified
#     by diffing the merge result against the new tag's pristine source
#     (each merge produced zero conflicts and isolated exactly the
#     project's own hunks, nothing more). `hfs-556.100.11` was NOT
#     touched -- it's actually *newer* than the manifest's own
#     `hfs-556.60.1`, an ambiguous case (unlike the other two, which were
#     unambiguously stale) that needs a deliberate keep-or-revert decision
#     rather than a mechanical fix; see the doc for detail.
#     `IOUSBFamily-630.4.5` also NOT touched -- confirmed no Big Sur-era
#     tag exists in the manifest for it at all (checked, not assumed), so
#     there is no better tag to move to.
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
#   - ncurses, libresolv, libiconv, OpenPAM, IOKitUser, gnutar,
#     diskdev_cmds, awk, IOKitTools, OpenSSH (2026-08-24, the
#     `docs/apple-oss-catalog.md` sweep): same `macos-112` manifest
#     method for all ten -- each tag below was confirmed via
#     `git ls-tree macos-112 -- <name>` matching a real release tag's
#     dereferenced commit on `git ls-remote --tags`, not guessed. Actual
#     build scripts (`tools/userland_staging/build_*.sh`) for these are
#     being written incrementally, same as every prior entry in this
#     file -- a pin here does not by itself mean a working build yet.
#   - llvm-libcxx / llvm-libcxxabi (Phase 6.5 Phase C, 2026-08-25): dyld's
#     C++ source needs a real libc++/libc++abi (`std::string`,
#     `std::vector`, `<iostream>`, etc.) to compile at all, and neither is
#     open-sourced by Apple as a standalone repo for this OS era --
#     confirmed directly (`git ls-remote` 404s on both
#     apple-oss-distributions/libcxx and .../libcxxabi; PureDarwin's org
#     only has `libcppabi`, the C++ ABI/RTTI support library, not the STL
#     itself). Per this project's source-priority rule (Apple real source
#     first, real open-source fallback second, custom last resort): the
#     real fallback here is LLVM upstream's own `libcxx`/`libcxxabi`
#     (Apache-2.0 w/ LLVM exception) -- the actual codebase Apple's
#     shipped libc++ is downstream of, just not packaged as its own
#     apple-oss-distributions repo. Pinned to `llvmorg-11.1.0`, matching
#     Xcode 12's Apple clang 1200.0.32.2 (~LLVM 11 lineage). Cloned via a
#     dedicated sparse-checkout block below, NOT the generic per-repo loop
#     -- `llvm/llvm-project` is a multi-GB monorepo (clang/lld/compiler-rt/
#     etc. all in one tree); a plain full clone would violate this
#     project's "small, real, reproducible" convention for no benefit,
#     since only the `libcxx`/`libcxxabi` subdirectories are needed.
#
#   - dyld: pinned to `dyld-940`, NOT the macos-112-manifest-matching
#     `dyld-832.7.3` this was originally pinned to. Real, deliberate
#     re-pin (2026-08-25, see docs/daemon-roadmap.md's Phase 6.5 Phase C
#     section for the full story): dyld-832.7.3's own dyld2.cpp
#     references a `dyld3::Loader` class that was confirmed -- by
#     checking the FULL git history of every dyld tag from dyld-832.7.3
#     through dyld-1378 -- to never exist in any released
#     apple-oss-distributions dyld snapshot at all (a transitional,
#     never-publicly-released class from whatever internal pre-release
#     build that exact tag was cut from). `dyld-940` is the EARLIEST
#     tag with a real, complete Loader implementation (Apple's dyld4
#     rewrite, `dyld/Loader.cpp`+`JustInTimeLoader.cpp`+
#     `PrebuiltLoader.cpp` -- a different, incompatible architecture
#     from dyld-832.7.3's dyld2/dyld3/ImageLoader split, not an
#     extension of it). This is a real, necessary exception to this
#     project's usual "match the macos-112 manifest exactly" rule --
#     the manifest's own dyld pin is provably unbuildable for real,
#     confirmed reasons, not a convenience shortcut.
#
# Usage: ./setup_third_party.sh [--force]
set -euo pipefail
cd "$(dirname "$0")"

FORCE=0
if [ "${1:-}" = "--force" ]; then
    FORCE=1
fi

mkdir -p third_party

#   - BSM (Phase 6.8, launchd+XPC): pinned to `bsm-13` (real
#     apple-oss-distributions repo, needed for `bsm/auditd_lib.h`/
#     `bsm/libbsm.h`, real launchd/libxpc dependencies per
#     docs/daemon-roadmap.md's Phase 6.8 file survey).
#   - ICU (CoreFoundation's ICU wall, docs/backlog.md item d7): same
#     `macos-112` manifest method -- `git ls-tree macos-112 -- ICU` ==
#     commit f7b86563706f76d5267452815c6b6121cf4698b7 == tag `ICU-66109`
#     (confirmed via `git ls-remote --tags`). Real, unmodified Apple ICU
#     (their icu4c fork, ~190MB checkout -- icuSources/ is stock upstream
#     ICU4C autotools/configure, plus cldrFiles/ locale data and Apple's
#     own ICU.xcodeproj for their internal B&I pipeline, which this
#     project does not use). Confirmed via `configure.ac` that this is
#     upstream ICU4C's real `--with-cross-build=<dir>` two-stage
#     cross-compile mechanism (build ICU natively for the host first to
#     get `config/icucross.mk` + generated data/tools, then
#     `--with-cross-build` that directory when configuring the real
#     target build) -- categorically different from this project's usual
#     from-scratch clang-per-file-loop template (see `build_libc_regex.sh`)
#     since ICU already ships its own real build system and generates
#     locale/charset data tables at build time; do not reinvent that by
#     hand. `ICU.patch` fixes a real upstream (Apple) oversight found
#     while getting the host-native build stage working (see
#     tools/userland_staging/build_icu_host.sh's own header comment):
#     `common/Makefile.in`'s autotools file list never got
#     `aaplbfct.cpp` added alongside `localsvc.cpp`, even though
#     `ICU.xcodeproj`'s real target already builds both -- a plain
#     `./runConfigureICU && make` hits an undefined-symbol link error for
#     `AppleLanguageBreakFactory` without it. Not yet built for the
#     target; this pin exists so the source is reproducible from a fresh
#     clone while that build work proceeds.
#   - libxml2 (CoreFoundation's CFXMLInterface.c, docs/backlog.md item
#     d7's full-sweep pass): same `macos-112` manifest method --
#     `git ls-tree macos-112 -- libxml2` == commit
#     5809e9164980d9e3bd6bb206ca8d00acb65514aa == tag `libxml2-34.9`
#     (confirmed via `git ls-remote --tags`), a real, unmodified Apple
#     fork of real upstream libxml2 2.9.4. Ships a real pregenerated
#     `Pregenerated Files/include/config.h` (a real `configure` run's
#     output, checked in for Apple's own Xcode-based iOS/macOS build) --
#     unlike ICU, no need to run `configure` at all, matching this
#     project's usual from-scratch clang-per-file-loop template instead
#     of ICU's own build system. The Xcode 12 iPhoneOS SDK's own bundled
#     libxml2 headers are the exact same real version (2.9.4, confirmed
#     via `LIBXML_DOTTED_VERSION`), so no header-version mismatch either.
#     What actually blocked `CFXMLInterface.c` before this pin existed
#     was never libxml2 at all -- a real, tiny, unrelated bug in
#     `swift-corelibs-foundation` itself (`CFXMLInterface.h` declares 3
#     functions returning `Boolean`, `CFXMLInterface.c` defines them
#     returning `bool`), fixed via a second commit in the existing
#     `swift-corelibs-foundation.patch`.
#   - PureDarwin (Phase 6.8, launchd+XPC source): sparse-checked-out below
#     (same pattern as llvm-project), NOT the generic REPOS loop -- see
#     that block's own comment for why this superseded the originally
#     scoped `PureDarwin/XPC` archived-repo source (real, verified finding
#     2026-08-25: PureDarwin/PureDarwin's `next` branch is an actively
#     maintained monorepo that already boots real launchd to a shell on
#     this exact SoC, bcm2837/Raspberry Pi 3, under QEMU -- a categorically
#     better reference than the frozen 2014 `launchd-842.91.1` + iXsystems
#     skeleton source docs/daemon-roadmap.md originally scoped this phase
#     around).
#
# name | clone_url | ref (branch or tag) | patch file (optional, relative to third_party-patches/)
REPOS='
xnu-7195|git@github.com:anurodhp/xnu-pi3-macos11.git|bcm2837-bringup|
xnu-latest|git@github.com:anurodhp/xnu-pi3-latest.git|main|
qemu-zhuowei|git@github.com:anurodhp/qemu-xnu-arm.git|master|
dtc-AppleDeviceTree|https://github.com/darwin-on-arm/dtc-AppleDeviceTree.git|master|dtc-AppleDeviceTree.patch
u-boot|https://github.com/FlorentRevest/u-boot.git|master|u-boot.patch
hfs|https://github.com/apple-oss-distributions/hfs.git|hfs-556.100.11|
IOStorageFamily|https://github.com/apple-oss-distributions/IOStorageFamily.git|IOStorageFamily-260.40.5|
IOUSBFamily|https://github.com/apple-oss-distributions/IOUSBFamily.git|IOUSBFamily-630.4.5|
IONetworkingFamily|https://github.com/apple-oss-distributions/IONetworkingFamily.git|IONetworkingFamily-151.40.1|
msdosfs|https://github.com/apple-oss-distributions/msdosfs.git|msdosfs-435.60.1|
dyld|https://github.com/apple-oss-distributions/dyld.git|dyld-940|
Libc|https://github.com/apple-oss-distributions/Libc.git|Libc-1439.40.11|
libpthread|https://github.com/apple-oss-distributions/libpthread.git|libpthread-454.80.2|
libplatform|https://github.com/apple-oss-distributions/libplatform.git|libplatform-254.80.2|
libdispatch|https://github.com/apple-oss-distributions/libdispatch.git|libdispatch-1271.40.12|libdispatch.patch
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
ncurses|https://github.com/apple-oss-distributions/ncurses.git|ncurses-57|
libresolv|https://github.com/apple-oss-distributions/libresolv.git|libresolv-68|
libiconv|https://github.com/apple-oss-distributions/libiconv.git|libiconv-59|
OpenPAM|https://github.com/apple-oss-distributions/OpenPAM.git|OpenPAM-28.40.1|
IOKitUser|https://github.com/apple-oss-distributions/IOKitUser.git|IOKitUser-1845.81.1|
gnutar|https://github.com/apple-oss-distributions/gnutar.git|gnutar-455|
diskdev_cmds|https://github.com/apple-oss-distributions/diskdev_cmds.git|diskdev_cmds-667.40.1|
awk|https://github.com/apple-oss-distributions/awk.git|awk-27.40.1|
IOKitTools|https://github.com/apple-oss-distributions/IOKitTools.git|IOKitTools-114.40.1|
OpenSSH|https://github.com/apple-oss-distributions/OpenSSH.git|OpenSSH-240.40.1|
Libnotify|https://github.com/apple-oss-distributions/Libnotify.git|Libnotify-279.40.4|
BSM|https://github.com/apple-oss-distributions/BSM.git|bsm-13|
ICU|https://github.com/apple-oss-distributions/ICU.git|ICU-66109|ICU.patch
libxml2|https://github.com/apple-oss-distributions/libxml2.git|libxml2-34.9|
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

# llvm-project: sparse-checkout only libcxx/libcxxabi (see the header
# comment above) -- deliberately NOT in the generic REPOS loop above,
# since a plain `git clone --branch` of the whole monorepo would pull
# clang/lld/compiler-rt/every other LLVM subproject along with it.
LLVM_REF="llvmorg-11.1.0"
LLVM_DEST="third_party/llvm-project"
if [ -d "$LLVM_DEST" ]; then
    if [ "$FORCE" = "1" ]; then
        echo "+ removing existing $LLVM_DEST (--force)"
        rm -rf "$LLVM_DEST"
    else
        echo "= $LLVM_DEST already exists, skipping (use --force to re-clone)"
        LLVM_DEST=""
    fi
fi
if [ -n "$LLVM_DEST" ]; then
    echo "+ sparse-cloning llvm-project @ $LLVM_REF (libcxx/libcxxabi only)"
    git clone --filter=blob:none --no-checkout --depth 1 --branch "$LLVM_REF" \
        https://github.com/llvm/llvm-project.git "$LLVM_DEST"
    git -C "$LLVM_DEST" sparse-checkout init --cone
    git -C "$LLVM_DEST" sparse-checkout set libcxx libcxxabi
    git -C "$LLVM_DEST" checkout "$LLVM_REF"
fi

# PureDarwin: sparse-checkout only src/Libraries/XPC (launchd/libxpc/
# notify/libinfo/libnv/launchctl/logd), src/Libraries/CrashReporterClient
# (a real, trivial 13-line dependency of libxpc -- just a static
# annotation struct, no real crash-reporting daemon), and cmake/ (kept for
# reference only -- this project's build_*.sh scripts hand-translate the
# real per-target file lists/mig() invocations rather than running cmake
# itself, same treatment every other Xcode-project-only source in this
# project already gets). `next` is PureDarwin's own active-development
# branch (`main` is frozen releases per their own README) -- no tag, HEAD
# tracked directly, matching this project's llvm-project precedent for
# "no meaningful tag to pin, track a specific real commit's tree instead."
PUREDARWIN_REF="next"
PUREDARWIN_DEST="third_party/PureDarwin"
if [ -d "$PUREDARWIN_DEST" ]; then
    if [ "$FORCE" = "1" ]; then
        echo "+ removing existing $PUREDARWIN_DEST (--force)"
        rm -rf "$PUREDARWIN_DEST"
    else
        echo "= $PUREDARWIN_DEST already exists, skipping (use --force to re-clone)"
        PUREDARWIN_DEST=""
    fi
fi
if [ -n "$PUREDARWIN_DEST" ]; then
    echo "+ sparse-cloning PureDarwin @ $PUREDARWIN_REF (src/Libraries/XPC, CrashReporterClient, cmake only)"
    git clone --filter=blob:none --no-checkout --depth 1 --branch "$PUREDARWIN_REF" \
        https://github.com/PureDarwin/PureDarwin.git "$PUREDARWIN_DEST"
    git -C "$PUREDARWIN_DEST" sparse-checkout init --cone
    git -C "$PUREDARWIN_DEST" sparse-checkout set src/Libraries/XPC src/Libraries/CrashReporterClient cmake
    git -C "$PUREDARWIN_DEST" checkout "$PUREDARWIN_REF"
fi

# CoreFoundation: real Apple source, but NOT from
# apple-oss-distributions/CF -- that repo's own last real tag
# (CF-855.17) is from 2014 (OS X Yosemite), 6 years stale relative to
# this project's Darwin 20.3/macOS 11.2 target, and confirmed absent
# from the real macos-112 distribution-macOS manifest entirely (CF is
# not a tracked component there at all -- verified via a real clone,
# not assumed). Real, actively-maintained, MUCH more recent CF source
# instead comes from apple/swift-corelibs-foundation's own vendored
# CoreFoundation/ subtree -- Apple's own cross-platform (Linux-buildable)
# fork of the same real CF codebase, kept building without the full
# macOS Objective-C runtime by design, which is exactly this project's
# own real requirement too. Pinned to the closest real tag to macOS
# 11.2 (Feb 2021): swift-5.4-DEVELOPMENT-SNAPSHOT-2021-02-12-a. Sparse-
# checkout only CoreFoundation/ -- a plain clone would pull the much
# larger Foundation/TestFoundation/Sources trees this project doesn't
# need at all.
SCF_REF="swift-5.4-DEVELOPMENT-SNAPSHOT-2021-02-12-a"
SCF_DEST="third_party/swift-corelibs-foundation"
if [ -d "$SCF_DEST" ]; then
    if [ "$FORCE" = "1" ]; then
        echo "+ removing existing $SCF_DEST (--force)"
        rm -rf "$SCF_DEST"
    else
        echo "= $SCF_DEST already exists, skipping (use --force to re-clone)"
        SCF_DEST=""
    fi
fi
if [ -n "$SCF_DEST" ]; then
    echo "+ sparse-cloning swift-corelibs-foundation @ $SCF_REF (CoreFoundation/ only)"
    git clone --filter=blob:none --no-checkout --depth 1 --branch "$SCF_REF" \
        https://github.com/apple/swift-corelibs-foundation.git "$SCF_DEST"
    git -C "$SCF_DEST" sparse-checkout init --cone
    git -C "$SCF_DEST" sparse-checkout set CoreFoundation
    git -C "$SCF_DEST" checkout "$SCF_REF"
    echo "+ applying third_party-patches/swift-corelibs-foundation.patch to $SCF_DEST"
    git -C "$SCF_DEST" am "../../third_party-patches/swift-corelibs-foundation.patch"
fi

echo "done -- third_party/ populated. See docs/roadmap.md and README.md for what to build next."
