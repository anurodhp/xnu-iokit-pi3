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
#   - Every other repo this project CHANGES is also one of our own forks
#     (anurodhp/*, e.g. dtc-AppleDeviceTree, u-boot, Csu, libdispatch,
#     configd, chrony), cloned directly with the real upstream added as a
#     second remote (5th REPOS field). Nothing is patched after cloning:
#     the old third_party-patches/*.patch + `git am` mechanism is gone.
#     Standing rule (2026-09-24): changes go into our fork, committed and
#     pushed, never into a patch file. The last two patch files
#     (u-boot.patch, dtc-AppleDeviceTree.patch) were deleted once a
#     tree-hash replay showed each fork's HEAD is byte-identical to
#     upstream + that patch. If you need to change a repo that is NOT yet
#     one of our forks, fork it first. The history of the three Csu fixes
#     (environ/__progname/NXArgc/NXArgv globals, then __mod_init_func
#     support for ifconfig) now lives in anurodhp/Csu.
#   - Libc: originally unmodified Apple source except `Libc.patch`
#     (2026-08-27) -- `locale/xlocale.c`'s own `__xlocale_init()`
#     (registers the real TSD key `__global_locale`/`__locale_key` need)
#     is only ever called from real Apple's own `_libc_initializer()`,
#     which this project has never ported. Without it `__locale_key`
#     stays at its static -1 sentinel forever, and `xlocale_private.h`'s
#     `__current_locale()` (called by real `vsnprintf()`, among others)
#     does an unchecked, out-of-bounds TSD lookup with that -1 on any
#     non-simulator target -- a real SIGSEGV, found via a real QEMU
#     crash the first time anything in this project called plain
#     snprintf()/vsnprintf() at runtime (syslogd's write_boot_log(),
#     Phase 6.7). The patch added a same-translation-unit constructor
#     calling the existing real `__xlocale_init()`, so it's linked in for
#     free by any of this project's 39+ build scripts that pull in this
#     object file.
#
#     2026-09-07 (DAR-206): cloned from this project's own fork
#     (git@github.com:anurodhp/Libc.git, branch iokit-port) instead of
#     upstream + a `git am` patch-file step -- the same real fix now
#     lives as real committed history on that branch, plus a third
#     commit (DAR-234) that was real, finished, already-reasoned-through
#     work sitting uncommitted in the working tree at conversion time
#     (`locale/FreeBSD/collate.c`/`locale/xlocale_private.h`, gating
#     `__collate_collating_symbol`/`__collate_equiv_class`'s widened
#     visibility behind a `COLLATE_STATE_EXPORTED` macro, same shape as
#     the existing `XLOCALE_STATE_EXPORTED` gate) -- committed for real
#     as part of this conversion rather than left to rot uncommitted.
#     `third_party-patches/Libc.patch` is no longer read by this script
#     -- kept in the tree only as a historical record.
#   - dyld: originally unmodified Apple source except `dyld.patch`
#     (2026-09-01, DAR-191) -- `libdyld/libdyldGlue.cpp`'s real,
#     unmodified `_dyld_initializer()` unconditionally self-registers a
#     `pthread_atfork()` triple for dyld's own dlopen lock. On this port
#     the child-side handler never returns after `fork()` (root-caused
#     via layered instrumentation, full trail in DAR-191 -- the
#     underlying libplatform/dyld lock-reset mechanism itself is real,
#     unmodified upstream code, not something this project can fix
#     without a materially bigger change to `third_party/dyld/dyld`'s
#     own `#if BUILDING_DYLD` internals), which silently hung every one
#     of launchd's forked daemon children forever (100% CPU, no panic,
#     no corpse) before they ever reached `bootstrap_init()`. The patch
#     skipped that one `pthread_atfork()` call -- a second, previously-
#     unknown instance of the exact hazard class already worked around
#     for the OTHER, direct dyld atfork trio (see
#     `tools/userland_staging/build_launchd_syscalls.sh`'s `fork_compat.c`,
#     docs/backlog.md d13); that earlier fix didn't know about this
#     separate, indirect registration path, so it kept firing regardless.
#
#     2026-09-07 (DAR-207): cloned from this project's own fork
#     (git@github.com:anurodhp/dyld.git, branch iokit-port) instead of
#     upstream + a `git am` patch-file step -- this is a real, non-
#     trivial behavioral patch against dyld's own fork-safety internals,
#     exactly the kind of change that benefits from a real fork's
#     history/diffability rather than a bare patch file. The same fix
#     now lives as real committed history on that branch, built on top
#     of the `dyld-940` tag. `third_party-patches/dyld.patch` is no
#     longer read by this script -- kept in the tree only as a
#     historical record.
#   - libdispatch: originally unmodified Apple source except
#     `libdispatch.patch` (3 commits, starting with moving the
#     `dispatch_pthread_root_queue_observer_hooks_s` typedef), applied
#     via `git am` after cloning upstream `libdispatch-1271.40.12`. Real
#     local history had already outgrown the patch file by the time of
#     conversion -- 4 real commits (DAR-153/167/257/259, covering
#     `libdispatch_init()` call-once ordering, a TSD-key race in the
#     dispatch manager thread's startup, the manager thread's wlh TSD key
#     initialization, and a real SIGSEGV in `_dispatch_source_invoke()`
#     from an unadopted workloop wlh) vs. the patch file's 3. 2026-09-07
#     (DAR-208): converted to cloning from this project's own fork
#     (git@github.com:anurodhp/libdispatch.git, branch iokit-port)
#     instead -- all 4 real fixes now live as real committed history on
#     that branch. `third_party-patches/libdispatch.patch` is no longer
#     read by this script -- kept in the tree only as a historical
#     record.
#   - Libinfo: originally unmodified Apple source except two real local
#     commits on top of the pinned `Libinfo-542.40.3` tag -- gating the
#     `mdns` module behind a new `MDNS_AVAILABLE` guard (mirrors the
#     existing `DS_AVAILABLE`/`MUSER_AVAILABLE` pattern already in
#     Apple's own source, DAR-181's original Libinfo port), and adding
#     `lookup.subproj/dns_module.c`, a new real `si_module` DNS resolver
#     backend gated behind a new `DNS_AVAILABLE` guard in the same shape
#     (DAR-306 -- this is what makes `getaddrinfo()` actually do real DNS
#     queries for the first time on this port). Both had been committed
#     only in this checkout's own local git history (`bcm2837-bringup-
#     local` anchor branch, no personal fork remote) -- silently lost on
#     a fresh `--force` re-clone.
#
#     2026-09-21 (DAR-307): cloned from this project's own fork
#     (git@github.com:anurodhp/Libinfo.git, branch iokit-port) instead of
#     upstream -- the same two real fixes now live as real committed
#     history on that branch, same pattern as Libc/dyld/libdispatch
#     above.
#   - libpthread: 2026-09-27 (DAR-354): cloned from this project's own
#     fork (git@github.com:anurodhp/libpthread.git, branch iokit-port)
#     instead of upstream. iokit-port = the manifest-pinned
#     `libpthread-454.80.2` tag (b4bdbdd) plus one commit, 58a80c8
#     (DAR-257: pthread_getspecific()/pthread_setspecific() TSD storage
#     mismatch for dynamically allocated keys, src/pthread_tsd.c). That
#     commit had lived only on a local `bcm2837-bringup` branch with no
#     fork remote, so a fresh clone silently dropped the fix.
#   - IOGraphics: 2026-09-27 (DAR-355): cloned from this project's own
#     fork (git@github.com:anurodhp/IOGraphics.git, branch iokit-port).
#     iokit-port = the manifest-pinned `IOGraphics-585` tag (9d031bd) plus
#     ten commits, one per compile-gap fix that
#     tools/patch_iographics_gaps.sh (DAR-331) used to apply in place to
#     the gitignored checkout. Each commit message carries that fix's
#     citations. The script is retired, and
#     tools/vendor_iographics_into_xnu.sh now fails fast if the checkout
#     lacks the fixes.
#   - IOPCIFamily: added 2026-09-08 (DAR-244), headers only. There is no
#     PCI bus on BCM2837 and this project will never have one -- the pin
#     exists because IOUSBFamily's IOUSBControllerV3.h declares an
#     `IOPCIDevice *_device` member and IOUSBController.cpp references
#     IOPCIDevice in 8 places, so the header has to resolve for the
#     family to compile at all. Unlike IOUSBFamily/Libm/BSM this one IS
#     manifest-verified: `distribution-macOS` at `macos-112` pins
#     IOPCIFamily at commit b6412a0416f0e9498dafb5d3ee7952e0b1248009,
#     whose own commit message is "IOPCIFamily-428.80.2" -- that is the
#     tag pinned here, resolved from the manifest rather than picked by
#     recency. Checked for the IOUSBFamily-630.4.5 empty-file failure
#     mode too; IOKit/pci's six headers are all real.
#   - IOSerialFamily: added 2026-09-08 (DAR-243), headers only. There are
#     no serial-port devices in this project's roadmap; the pin exists
#     purely because `AppleUSBCDC.cpp` -- the CDC master driver that
#     BOTH AppleUSBCDCECMControl and AppleUSBCDCECMData depend on --
#     includes <IOKit/serial/IOSerialKeys.h>, IOSerialDriverSync.h,
#     IOModemSerialStreamSync.h and IORS232SerialStreamSync.h for its
#     ACM (modem/serial) sibling path. Same "no manifest entry" ceiling
#     as IOUSBFamily/Libm/BSM: checked `distribution-macOS` at
#     `macos-112` and IOSerialFamily is not listed there at all, so
#     `IOSerialFamily-93.200.2` is simply the newest real tag (the repo
#     has three: 91, 93, 93.200.2).
#   - IOHIDFamily: added 2026-09-21 (DAR-331), ONE header. This board has
#     no HID hardware and no window server, and docs/apple-oss-catalog.md
#     correctly files the rest of IOHIDFamily as structurally inapplicable
#     here. The pin exists because IOGraphics cannot be compiled without
#     it: IOGraphicsFamily/IOKit/graphics/IOFramebufferShared.h:26
#     `#include <IOKit/hidsystem/IOHIDTypes.h>` unconditionally, for the
#     IOGPoint/IOGBounds cursor geometry types that
#     StdFBShmem_t/IOFramebuffer's cursor state are declared in terms of.
#     Every IOGraphics source that reaches IOFramebuffer.h therefore stops
#     at that include with a hard `file not found` before any other gap is
#     even visible (confirmed by compiling, not assumed). Manifest-
#     verified like IOPCIFamily: `distribution-macOS` at `macos-112`
#     (release.json:105-106) names `IOHIDFamily-1633.81.2`, which is the
#     tag pinned here -- resolved from the manifest, not picked by
#     recency. UPDATE 2026-09-24 (DAR-245): the kernel half is now being
#     ported for USB keyboard input (see docs/apple-oss-catalog.md). All 91
#     kext sources compile via tools/iohid_gap_audit.sh.
#     2026-09-27 (DAR-356): cloned from this project's own fork
#     (git@github.com:anurodhp/IOHIDFamily.git, branch iokit-port).
#     iokit-port = `IOHIDFamily-1633.81.2` (4a76dd9) plus five commits,
#     one per source fix that tools/patch_iohid_gaps.sh (fixes 1-5) used to
#     apply in place. Two of them bring in files from OLDER tags as
#     ordinary commits: AppleHIDUsageTables.h from 1035.70.7 and the
#     FastPath user client from 1633.60.4. So a fresh clone no longer
#     fetches extra tags. The IOUSBHIDDriver.cpp fixes it went on to stage
#     (6/6b) are commits on anurodhp/IOUSBFamily now, and the script is
#     retired (DAR-421).
#   - IOUSBFamily: 2026-09-28 (DAR-421): cloned from this project's own
#     fork (git@github.com:anurodhp/IOUSBFamily.git, branch iokit-port).
#     iokit-port = `IOUSBFamily-560.4.2` (aa1bde6) plus seven commits, one
#     per source fix that used to be applied to staged or vendored copies:
#     tools/build_iousbfamily.sh's IOUSBDevice.cpp fix,
#     tools/vendor_usb_into_xnu.sh FIXES 1/1b/1c/1d/4, and
#     tools/patch_iohid_gaps.sh fixes 6/6b (IOUSBHIDDriver.cpp). Same code
#     as before; only the in-source comments changed (they cite the fork
#     instead of those scripts). The pin history
#     below (630.4.5 -> 560.4.2) still applies: the fork is based on 560.4.2.
#   - hfs, IOStorageFamily, IOUSBFamily (until DAR-421, above), IONetworkingFamily, msdosfs: unmodified Apple
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
#     CORRECTED AGAIN 2026-09-08 (DAR-244): `IOUSBFamily-630.4.5` (the
#     newest tag the repo has) publishes its ten most important source
#     files as literal ZERO-BYTE files -- `IOUSBController.cpp`,
#     `IOUSBControllerV2.cpp`, `IOUSBControllerV3.cpp`,
#     `IOUSBControllerUserClient.cpp`, `IOUSBController_Errata.cpp`,
#     `IOUSBDevice.cpp`, `IOUSBInterface.cpp`, `IOUSBPipeV2.cpp`,
#     `IOUSBRootHubDevice.cpp`, `IOUSBWorkLoop.cpp`, plus headers
#     `IOUSBController.h`, `IOUSBControllerV3.h`, `IOUSBControllerUserClient.h`,
#     `IOUSBLog.h`, `IOUSBPriv.h`, `USB.h`, `IOUSBAppleIDs.h`,
#     `IOUSBWorkLoop.h` and all three `AppleUSBHub/Classes/*.cpp`.
#     Confirmed against real upstream, not just the local clone: the
#     GitHub contents API for that ref reports size 0 for exactly those
#     paths. The previous "pin the newest available real source" call
#     was correct in spirit but landed on a tag with no source in it --
#     "newest tag" and "newest tag that actually contains the code" are
#     not the same thing, and nothing checked the difference because
#     IOUSBFamily was never compiled by any build script (DAR-244).
#     Re-pinned to `IOUSBFamily-560.4.2`, the next-newest tag, which has
#     all of them for real (IOUSBController.cpp 170KB/4779 lines,
#     IOUSBDevice.cpp 165KB/4771 lines, IOUSBInterface.cpp 82KB/2550
#     lines, IOUSBControllerV3.cpp 86KB, IOUSBRootHubDevice.cpp 42KB,
#     ... 24804 lines of real source across Classes/ total, zero empty
#     files). Still outside the `macos-112` manifest -- the honest
#     ceiling above is unchanged -- but this is the newest tag that is
#     actually usable as source. Real consumer as of DAR-243/DAR-244:
#     the IOUSBController/IOUSBDevice/IOUSBInterface/IOUSBPipe layer
#     `AppleUSBCDCECM` needs. NOTE: `AppleUSBCDCDriver-4303.2.1` (DAR-448:
#     now anurodhp/AppleUSBCDCDriver iokit-port, that tag plus two fix
#     commits) still
#     `#include`s the legacy `<IOKit/usb/IOUSBDevice.h>`/`IOUSBPipe.h`/
#     `IOUSBInterface.h`/`USB.h` family (checked in its own source, not
#     assumed from the version number), so the legacy IOUSBFamily is the
#     right partner for it, not the closed-source IOUSBHostFamily.
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
#     project notes assumed no such manifest existed; it does. `bash`
#     itself gained real local fix commits on top of its `bash-123.40.1`
#     tag (`smatch.c`'s `__collate_lookup` prototype-conflict fix, plus
#     two real READLINE/HAVE_TERMCAP_H toggles), carried for a while as
#     `bash.patch` applied via `git am` -- see the DAR-209 note further
#     below for why that patch had already drifted and how it was
#     converted to a real owned fork.
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
#     via `git ls-remote --tags`). Real Apple `cron`/`crontab` source,
#     carrying real local fix commits on top of the tag (`do_command.c`
#     dropping IOKit/CoreFoundation/vproc/launchd bootstrap glue this
#     project has no equivalent for; a `pthread_once()`-based
#     `COMPAT_MODE` init race and a broken `setlinebuf()` call, both real
#     crashes) -- originally carried as `cron.patch` applied via `git am`
#     after cloning upstream. 2026-09-07 (DAR-210): converted to cloning
#     from this project's own fork (git@github.com:anurodhp/cron.git,
#     branch iokit-port) instead -- the same fixes now live as real
#     committed history on that branch. `third_party-patches/cron.patch`
#     is no longer read by this script -- kept in the tree only as a
#     historical record. `ntp` was investigated the same way and
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
#   - chrony (DAR-169, 2026-09-14): the real ntp-sync answer that
#     replaced the "not yet pinned" `contrib/ntp` idea above -- chrony
#     already ships a real Darwin/macOS backend (`sys_macosx.c`) built on
#     the same real `ntp_adjtime()`/`adjtime()`/`settimeofday()` BSD
#     syscalls this project's own xnu-7195 already implements, and is
#     upstream's own actively-recommended ntpd replacement (also true on
#     real modern macOS -- Homebrew's chrony formula exists for exactly
#     this reason). Not an Apple open-source project, so no
#     apple-oss-distributions manifest entry to match against; cloned
#     from this project's own fork (git@github.com:anurodhp/chrony.git,
#     branch `iokit-port`, tag `4.9`) per the standing "gitrepo" rule for
#     unowned upstreams (see the `dyld`/`Libc`/`cron` entries above for
#     the same pattern) -- no local patches needed yet, so `iokit-port`
#     currently just tracks the real `4.9` tag verbatim. See
#     `tools/userland_staging/build_chrony.sh`'s own header comment for
#     the full real build story.
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
#   - libedit (DAR-283, 2026-09-10): real Apple `scutil(8)` links
#     `libedit.dylib` -- confirmed against configd's own
#     `configd.xcodeproj/project.pbxproj`, whose `scutil-Embedded`
#     PBXNativeTarget's Frameworks build phase lists exactly
#     CoreFoundation.framework + SystemConfiguration.framework +
#     libedit.dylib -- and `scutil.h` unconditionally `#include`s
#     `<histedit.h>`, which the pinned Xcode-12 iPhoneOS SDK does not
#     ship at all. Same `macos-112` manifest method as every pin above:
#     `distribution-macOS`'s `release.json` at tag `macos-112` lists
#     `{"project": "libedit", "tag": "libedit-55"}` (fetched and read
#     directly, not guessed from the newest tag -- `libedit-57`,
#     `libedit-62` and `libedit-65` all exist upstream and are all
#     WRONG for Darwin 20.3).
#     FULLY BUILT as of DAR-286 (2026-09-11). This used to carry a scope
#     note saying only `src/histedit.h` was used and the library itself
#     was not built; that is no longer true.
#     `tools/userland_staging/build_libedit.sh` builds all 29 real source
#     files at full Apple WIDECHAR parity into
#     `libc_build/system/libedit.dylib` (install_name
#     `/usr/lib/libedit.3.dylib`), regenerating the AWK `makelist`
#     headers and asserting them byte-identical to Apple's own
#     pre-generated `local/` copies, and `scutil.macho` links it for
#     real. Read that script's header for the terminfo and
#     no-canonical-owner-for-fork details.
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
#     clone while that build work proceeds. The fix later grew to 3
#     commits total (a matching `i18n/Makefile.in` build-list fix for
#     `uameasureformat.cpp`, plus a `TARGET_OS_EMBEDDED` include guard),
#     carried as `ICU.patch` applied via `git am` after cloning upstream.
#     2026-09-07 (DAR-212): converted to cloning from this project's own
#     fork (git@github.com:anurodhp/ICU.git, branch iokit-port) instead --
#     the same 3 fixes now live as real committed history on that branch.
#     `third_party-patches/ICU.patch` is no longer read by this script --
#     kept in the tree only as a historical record.
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
#   - cctools (CoreFoundation's NXFindBestFatArch/NXGetLocalArchInfo gap,
#     docs/backlog.md item d7): NOT part of the `macos-112` manifest at
#     all (confirmed via a real `git show macos-112:.gitmodules` --
#     cctools ships with Xcode/the Command Line Tools, not the OS
#     distribution, same category as llvmCore above). Pinned instead to
#     `cctools-973.0.1`, confirmed via this project's own pinned
#     `/Applications/Xcode-12.app` toolchain's real
#     `otool --version` output ("Apple Inc. version cctools-973.0.1") --
#     the exact cctools these two functions need to match, not a guess.
#     Only `libmacho/arch.c` is needed (real, unmodified, confirmed by a
#     real trial compile to need nothing beyond this project's existing
#     Mach-port stack: `host_info`/`mach_host_self`
#     (`libmach_ipc.a`), `mach_port_deallocate`/`mach_task_self_`
#     (`libmalloc.a`) -- zero new gaps).
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
#   - syslog (Phase 5.5 item 2 / daemon-roadmap Tier 1, 2026-08-27): same
#     `macos-112` manifest method -- `git ls-tree macos-112 -- syslog` ==
#     commit 19b7828778996ac0d6d8c6c13d01c816677c5f03 == tag `syslog-385`
#     (confirmed via `git ls-remote --tags`, matching
#     `docs/apple-oss-catalog.md`'s earlier manifest-sweep finding, now
#     directly re-verified rather than just cited). Real, unmodified
#     Apple `syslogd`/`syslog`/`aslmanager` source. Classic BSD `syslogd`
#     listens on a Unix-domain socket (`/var/run/syslog`), NOT a Mach
#     service -- no bootstrap-namespace/launchd dependency at all, unlike
#     `notifyd` (see `docs/daemon-roadmap.md`'s Phase 6.7 section).
#
#   - BSM (2026-08-27, tools/userland_staging/build_bsm.sh): pinned earlier
#     (Phase 6.8, see the BSM entry above) but not built until this pass --
#     same "proactively port a small, dependency-light Apple library
#     before something else needs it" spirit as Libm below. Same "no
#     manifest entry" honest ceiling as Libm/IOUSBFamily -- checked
#     distribution-macOS's macos-112 manifest directly (full 232-entry
#     tree, git ls-tree), no entry named BSM or audit exists at all.
#     bsm-13 confirmed the newest real tag (git ls-remote --tags:
#     bsm-13 > bsm-8 > bsm-2.11 > bsm-2.10). `BSM.patch` (new, applied via
#     git am like Csu.patch/Libc.patch/ICU.patch) fixes a real, confirmed
#     cross-repo API-vintage skew: xnu-7195's own bsd/bsm/audit_record.h
#     declares a full "deprecated userland BSM API" prototype block (9
#     functions: au_to_arg/data/groups/opaque/file/path/seq/exec_args/
#     exec_env) with slightly different signatures than what BSM-13's own
#     bsm_token.c defines (mostly const-qualifier drift, one real
#     behavioral difference -- au_to_file gained an extra struct timeval
#     parameter in the newer header) -- fixed by aligning BSM-13's
#     definitions to match xnu-7195's real, currently-published contract.
#     See build_bsm.sh's own header comment for the full real-gap writeup
#     (MAX_GROUPS/HEADER_SIZE/AU_*_TOKEN naming-drift compat header,
#     be16/32/64enc, real audit(2)/auditon(2)/getaudit(2) syscall
#     wrappers) -- all 11 real bsm/lib/*.c files compile clean, zero
#     exclusions, real libbsm.a + a real, fully-static, zero-undefined-
#     symbol smoketest.
#
#     2026-09-07 (DAR-211): converted to cloning from this project's own
#     fork (git@github.com:anurodhp/bsm.git, branch iokit-port) instead
#     of upstream + the `git am` patch-file step described above -- the
#     same real fix now lives as real committed history on that branch.
#     `third_party-patches/BSM.patch` is no longer read by this script --
#     kept in the tree only as a historical record.
#
#   - Libm (2026-08-27): NOT manifest-verifiable the way every other pin
#     above is -- checked `distribution-macOS`'s `release.json` at
#     `macos-112` AND every other macOS tag back through `macos-1012`
#     (Sierra) and it never lists a `Libm` project at all (188 real
#     entries at `macos-112`, confirmed by parsing the JSON, zero of them
#     named `Libm`). Apple's `apple-oss-distributions/Libm` repo does
#     exist and is real, but its real tag list (`git ls-remote --tags`)
#     has only 15 tags total, and its own tag *objects'* real tagger
#     dates (`git cat-file tag <sha>`, not GitHub's own commit-import
#     dates, which are unreliable for these bulk-imported repos) run
#     1(2003) through `Libm-2026` at 2011-07-20 -- Apple has not
#     published Libm source at all since OS X Lion, a full decade before
#     Darwin 20.3/macOS 11.2 and about 2 years before arm64 existed as an
#     architecture. `Libm-2026` is pinned anyway as the real, newest
#     available source (same "no era-accurate tag exists, pin the newest
#     real one and document the gap honestly" precedent as `IOUSBFamily`
#     above) -- but `tools/userland_staging/build_libm.sh` only actually
#     builds from its portable, hardware-agnostic C files (its
#     `Source/ARM` tree predates AArch64 and is a mix of real armv6/armv7
#     assembly plus portable C; the assembly is skipped outright, not
#     ported). See `docs/apple-oss-catalog.md` for the full real
#     manifest-sweep/tag-date method.
#
#   - configd (2026-09-08): same real local fix as always -- DAR-170's
#     `plugin_support.c` change treating IPConfiguration as a builtin
#     plugin (this port has no proven CFBundle-dlopen-of-a-.bproj path,
#     see that ticket) -- originally carried as `configd.patch` applied
#     via `git am` after cloning upstream `configd-1109.60.2`. Converted
#     to cloning from this project's own fork (git@github.com:anurodhp/
#     configd.git, branch iokit-port) instead -- the fix now lives as a
#     real committed history on that branch, past the same pinned
#     `configd-1109.60.2` tag. Two commits as of 2026-09-11: DAR-170's
#     `plugin_support.c` change above, plus DAR-285's one-line
#     `#ifndef my_log` guard around `dnsinfo/dnsinfo_internal.h`'s
#     `<SystemConfiguration/SCPrivate.h>` include (that include exists
#     only to supply SC_log, which is only used by the my_log fallback
#     right below it -- guarding it lets libresolv.dylib compile the real
#     `dnsinfo_copy.c` without depending on header trees staged by
#     scripts that link libresolv, i.e. without a circular build-order
#     dependency; every other includer is unaffected and still gets the
#     real SC_log). Because this row pins a BRANCH, not a SHA, a fresh
#     clone picks both up automatically -- there is no tag to bump here.
#     `third_party-patches/configd.patch`
#     is no longer read by this script -- removed from the tree (unlike
#     the historical-record patches above, nothing else in this repo
#     documents it independently, e.g. no sdcard/docs/sdcard-boot-notes.md-style
#     regeneration note, so keeping a dead copy around had no value).
#     NOTE: the fork's own `main` branch is a plain, unrelated mirror of
#     upstream (currently at `configd-1405.100.8`, far newer than the
#     pinned tag, no patch applied) -- always clone `iokit-port`
#     specifically, never `main`, for this repo.
#
#   - xxHash (DAR-331): IODisplay.cpp (part of the pinned IOGraphics-585
#     drop, above) calls XXH64() to hash EDID data into a display-
#     preferences key (IODisplay.cpp:441-442,662). xxHash is not Apple's
#     code and Apple never published it as a standalone repo -- a
#     third-party dependency Apple vendors internally, same situation as
#     llvm-libcxx above. Real upstream per the source-priority rule:
#     Cyan4973/xxHash (BSD 2-Clause). Pinned to `v0.8.2` (2023-07-21),
#     NOT an era-matched v0.8.0: v0.8.2 is the first release with
#     upstream's documented kernel/embedded mode (XXH_NO_STDLIB, plus
#     XXH_NO_XXH3 to drop the <arm_neon.h> include) -- v0.8.0
#     unconditionally includes <stdlib.h> and, on arm64, <arm_neon.h>,
#     neither usable in a -nostdinc kernel TU. XXH64's output is fixed by
#     the algorithm spec, so the version does not change the hash.
#     Header-only (XXH_INLINE_ALL); no build script. Consumed through
#     compat/IOKit/graphics/IOGraphicsXXHash.h, which documents the exact
#     configuration and the 2-arg vs 3-arg XXH64 arity gap.
#
#   - lzfse (DAR-331): IOFramebuffer.cpp:2317 declares, and :2390 calls,
#     `lzvn_decode(dst, dst_size, src, src_size)` to unpack the boot Apple
#     logo (AppleLogo.h/AppleLogo2X.h are LZVN-packed). On macOS that is a
#     kernel export; xnu-7195's published source has no LZVN code at all
#     (checked: `git grep lzvn_decode` finds nothing). Apple's own LZVN
#     decoder IS open source, inside Apple's LZFSE reference implementation
#     at github.com/lzfse/lzfse (BSD 3-Clause, Apple Inc.) -- not an
#     apple-oss-distributions repo, so this is the real-upstream fallback of
#     the source-priority rule. Pinned to `lzfse-1.0` (2017-03-08, the only
#     tag); every file used (src/lzvn_decode_base.[ch], lzfse_internal.h,
#     lzfse_fse.h, lzfse_tunables.h) is byte-identical at the repo's latest
#     commit. Only the decoder is compiled; see
#     tools/vendor_iographics_into_xnu.sh and
#     compat/IOKit/graphics/IOGraphicsLZVN.c for the 4-argument wrapper.
#
# text_cmds_bintrans: a second, deliberately post-macos-112 checkout of
#     text_cmds, used ONLY for bintrans/ -> /usr/bin/base64. Apple's
#     macOS 11 manifest pin (text_cmds-106 above) has no base64 source, and
#     no text_cmds tag does before text_cmds-165.0.4 (checked 118,
#     138.100.3, 154). bintrans/ there carries Apple's own classic base64
#     (apple_base64.c, compiled under __APPLE__ with the macOS
#     `base64 [-Ddh] [-b num] [-i in] [-o out]` interface). Kept separate so
#     the macOS 11 text_cmds used by every other command is not moved.
#     See docs/apple-oss-catalog.md.
# X.org client libraries (DAR-383, X11GS 2, 2026-09-26): not Apple code
#     at all -- Apple's own X11 (10.5-10.7) and XQuartz were built from
#     these same freedesktop.org sources, and none of them is an
#     apple-oss-distributions repo, so this is the source-priority rule's
#     real-upstream fallback. Each is pinned to the exact release tag the
#     nixpkgs PureDarwin builds from (nixos-26.05, rev 8f0500b9, pinned in
#     PureDarwin's flake.lock; versions read from nixpkgs'
#     pkgs/by-name/*/package.nix at that rev and each tag confirmed with
#     `git ls-remote`). Cloned unmodified -- nothing is forked; if one ever
#     needs a change it becomes an anurodhp/* fork first. Built by
#     tools/userland_staging/build_xorg_headers.sh and build_lib{Xau,
#     Xdmcp,xcb,X11,Xext,Xrender,Xfixes,Xrandr,Xcursor,ICE,SM,Xt,Xmu}.sh.
#     libXft-2.3.9 is pinned too but NOT built yet: it needs freetype +
#     fontconfig (DAR-384).
# Font/2D stack (DAR-384, X11GS 3, 2026-09-26):
#   zlib, expat -- Apple's own, apple-oss-distributions zlib-76 (upstream
#       zlib 1.2.11) and expat-26 (upstream expat 2.2.8): the exact
#       commits distribution-macOS's macos-112 manifest pins (its zlib and
#       expat submodules are 805f40328df8... and 37469d253ca9..., which
#       `git ls-remote` resolves to these two tags). Built into
#       /usr/lib/libz.1.dylib and /usr/lib/libexpat.1.dylib, where macOS 11
#       has them (tools/userland_staging/build_zlib.sh, build_expat.sh).
#   freetype, fontconfig, libpng, pixman, cairo, mkfontscale, libfontenc
#       -- not Apple code (Apple's own 10.5-10.7 X11 shipped the same
#       upstream projects in /usr/X11/lib), so the source-priority rule's
#       real-upstream fallback, at the release tags nixpkgs nixos-26.05
#       (rev 8f0500b9, PureDarwin's flake.lock pin) ships: freetype 2.14.3,
#       fontconfig 2.17.1, libpng 1.6.58, pixman 0.46.4, cairo 1.18.4,
#       mkfontscale 1.2.3, libfontenc 1.1.9 (versions read from nixpkgs'
#       package.nix files at that rev; every tag confirmed with
#       `git ls-remote`). Unmodified, nothing forked. nixpkgs' five
#       post-release FreeType security patches are not carried (see
#       build_freetype.sh). mkfontscale/libfontenc are built for the HOST
#       only, to write fonts.scale/fonts.dir at image-build time
#       (build_fonts.sh).
# X server (DAR-387, X11GS 4, 2026-09-26): xorg-server 21.1.24 (built as
#     Xvfb only), libXfont2 2.0.7, libxkbfile 1.1.3, xkbcomp 1.5.0 and
#     xkeyboard-config 2.47 -- the versions nixpkgs nixos-26.05 rev 8f0500b9
#     ships (pkgs/by-name/*/package.nix at that rev), same source-priority
#     reasoning as the X.org block above; every tag confirmed with
#     `git ls-remote`. Cloned unmodified. libfontenc (pinned above for the
#     host tools) is also built for the target here, for libXfont2.
#     X test clients (DAR-383/387): xdpyinfo 1.4.0, xwd 1.0.9, xev 1.2.6,
#     same nixpkgs rev, tags confirmed with `git ls-remote`; built by
#     tools/userland_staging/build_x11_clients.sh.
#     libXi 1.8.2 and libXtst 1.2.5 (same rev) for xdpyinfo, whose
#     configure.ac:41 requires xtst (libXtst itself needs xi).
#     libxcvt 0.1.3 (same rev, tag confirmed with `git ls-remote`): the Xorg
#     server's modeline library (DAR-389), required by xserver meson.build
#     when -Dxorg=true.
#     xdm 1.1.17, libXaw 1.0.16 and libXpm 3.5.19 (graphical login): the
#     newest release tag of each on its freedesktop.org repo, confirmed
#     with `git ls-remote --tags` 2026-09-28 (nixpkgs' rev is not the
#     constraint here: xdm and libXaw are not in the earlier X.org
#     pins, so newest release it is). libXaw's Pixmap.c includes
#     <X11/xpm.h> unconditionally, so it needs libXpm; xdm's greeter
#     needs libXpm for its logo and libXaw for the chooser. Cloned
#     unmodified; built by build_libXpm.sh, build_libXaw.sh, build_xdm.sh.
#     Window Maker 0.96.0 (DAR-424): upstream window-maker/wmaker, tag
#     wmaker-0.96.0 (latest release, confirmed with `git ls-remote`
#     2026-09-27). Apple ships no X window manager and FreeBSD's x11-wm/
#     windowmaker port builds this same upstream, so upstream is the
#     source. DAR-437: now anurodhp/wmaker branch iokit-port (upstream
#     0.96.0 + wrlib/fastpath.h: 32bpp TrueColor fast path and NEON);
#     built by tools/userland_staging/build_wmaker.sh. DAR-431: branch
#     dar-431-kqueue (off iokit-port): WUtil waits in a kqueue instead of
#     select(), with W_KQueueAddFilter for DAR-432/433.
#     libffi-27 (DAR-388): apple-oss-distributions/libffi, the manifest
#     pin -- `git ls-tree macos-112 -- libffi` in distribution-macOS ==
#     commit 491d23ac598f == tag libffi-27 (checked 2026-09-28). Cloned
#     unmodified; built by tools/userland_staging/build_libffi.sh.
#     gnustep-make 2.9.3 and gnustep-base 1.31.1 (DAR-388): our fork
#     anurodhp/tools-make branch dar-451-objc4-framework-classes (off
#     upstream tag make-2_9_3; DAR-451: target.make extracts objc4's
#     _OBJC_CLASS_$_ class names on Darwin, so NSFramework_<name> lists its
#     classes and framework resources are found), and our fork
#     anurodhp/libs-base branch dar-451-gworkspace (off iokit-port, off
#     upstream tag base-1_31_1), which carries the port to Apple's objc4
#     runtime plus DAR-451's gdnc stdio, getpw*_r result and BOOL-NSNumber
#     archiving fixes. GNUstep is the user's chosen GUI layer (DAR-381), and
#     Apple ships no GNUstep, so upstream is the source.
#     gnustep-gui 0.32.0 and gnustep-back 0.32.0 (DAR-390): released with
#     base 1.31 (2025-02). gui is our fork anurodhp/libs-gui branch
#     iokit-port (off upstream tag gui-0_32_0: typed IMPs, PACKAGE_SCOPE on
#     objc4); back is upstream tag back-0_32_0, unmodified.
#     gnustep-examples (DAR-390): our fork anurodhp/tests-examples branch
#     iokit-port, upstream master 46ef973 (2026-06-28) unmodified -- the
#     repo has no release tag newer than 2000, so the fork branch is what
#     pins the commit. Source of Ink.app (build_gnustep_ink.sh).
#     gnustep-gap (DAR-394): our fork anurodhp/gap branch iokit-port, off
#     gnustep/gap master 220860a9 (2026-09-08; gap has no release tags).
#     Source of Terminal.app (system-apps/Terminal): one commit adding the
#     Darwin pty include and link libraries. build_gnustep_terminal.sh.
#     gnustep-gworkspace (DAR-392): our fork anurodhp/apps-gworkspace
#     branch iokit-port, off gnustep/apps-gworkspace tag gworkspace-1_0_0
#     (2025-02-12, 14ea9f49): typed IMP function pointers at five call
#     sites for objc4; branch dar-451-daemon-start-wait adds a longer wait
#     for a just-launched fswatcher/ddbd to register (DAR-451). Source of GWorkspace.app, Recycler.app,
#     FSNode/DBKit and the ddbd/fswatcher tools (build_gnustep_gworkspace.sh).
#     libtiff 4.7.1 (DAR-390): gitlab.com/libtiff/libtiff tag v4.7.1,
#     unmodified; gnustep-gui requires it. Apple publishes no libtiff.
#     gnustep-eau-theme (DAR-461): our fork anurodhp/gershwin-eau-theme
#     branch iokit-port, off gershwin-desktop/gershwin-eau-theme main
#     3d741fbc (2026-09-30, LGPL-2.1), the Mac-like Eau theme, the default
#     GNUstep theme (GSTheme in GlobalDefaults.plist). The original
#     Savannah plugins/themes/Eau is on no GitHub mirror and savannah's
#     viewvc/svn returned 503 to scripts. Fork changes: no NSRegularExpression
#     (our base is --disable-icu), GWDialog categories via class_addMethod
#     (GWDialog is in the GWorkspace app, not linkable), -O2 (ARC block retains
#     crash here), EAU_NO_TEST guard.
#     build_gnustep_eau_theme.sh.
#   DejaVu fonts -- the upstream release tarball, not the git tag: the
#       tag (version_2_37, the nixpkgs version) holds only FontForge .sfd
#       sources, and building the TTFs needs fontforge + perl Font::TTF,
#       which this host does not have. The tarball is the DejaVu project's
#       own build of that tag, fetched below and checked against its
#       SHA-256.
# name | clone_url | ref (branch or tag) | (reserved, must be empty) | upstream_url (optional)
# The 4th field used to name a third_party-patches/ file to `git am`. That
# mechanism is retired (see the header); a non-empty value is an error.
REPOS='
xnu-7195|git@github.com:anurodhp/xnu-pi3-macos11.git|bcm2837-bringup|
xnu-latest|git@github.com:anurodhp/xnu-pi3-latest.git|main|
qemu-zhuowei|git@github.com:anurodhp/qemu-xnu-arm.git|master|
dtc-AppleDeviceTree|git@github.com:anurodhp/dtc-appledevicetree.git|master||https://github.com/darwin-on-arm/dtc-AppleDeviceTree.git
u-boot|git@github.com:anurodhp/u-boot.git|master||https://github.com/FlorentRevest/u-boot.git
hfs|https://github.com/apple-oss-distributions/hfs.git|hfs-556.100.11|
IOStorageFamily|https://github.com/apple-oss-distributions/IOStorageFamily.git|IOStorageFamily-260.40.5|
IOUSBFamily|git@github.com:anurodhp/IOUSBFamily.git|iokit-port||https://github.com/apple-oss-distributions/IOUSBFamily.git
IOPCIFamily|https://github.com/apple-oss-distributions/IOPCIFamily.git|IOPCIFamily-428.80.2|
IOSerialFamily|https://github.com/apple-oss-distributions/IOSerialFamily.git|IOSerialFamily-93.200.2|
AppleUSBCDCDriver|git@github.com:anurodhp/AppleUSBCDCDriver.git|iokit-port||https://github.com/apple-oss-distributions/AppleUSBCDCDriver.git
IONetworkingFamily|https://github.com/apple-oss-distributions/IONetworkingFamily.git|IONetworkingFamily-151.40.1|
IOGraphics|git@github.com:anurodhp/IOGraphics.git|iokit-port||https://github.com/apple-oss-distributions/IOGraphics.git
IOHIDFamily|git@github.com:anurodhp/IOHIDFamily.git|iokit-port||https://github.com/apple-oss-distributions/IOHIDFamily.git
xxHash|https://github.com/Cyan4973/xxHash.git|v0.8.2|
lzfse|https://github.com/lzfse/lzfse.git|lzfse-1.0|
msdosfs|https://github.com/apple-oss-distributions/msdosfs.git|msdosfs-435.60.1|
dyld|git@github.com:anurodhp/dyld.git|xdm-greeter-fix||https://github.com/apple-oss-distributions/dyld.git
Libc|git@github.com:anurodhp/Libc.git|iokit-port||https://github.com/apple-oss-distributions/Libc.git
libpthread|git@github.com:anurodhp/libpthread.git|iokit-port||https://github.com/apple-oss-distributions/libpthread.git
libplatform|https://github.com/apple-oss-distributions/libplatform.git|libplatform-254.80.2|
Libsystem|https://github.com/apple-oss-distributions/Libsystem.git|Libsystem-1292.60.1|
libdispatch|git@github.com:anurodhp/libdispatch.git|iokit-port||https://github.com/apple-oss-distributions/libdispatch.git
libclosure|https://github.com/apple-oss-distributions/libclosure.git|libclosure-78|
libmalloc|https://github.com/apple-oss-distributions/libmalloc.git|libmalloc-317.40.8|
objc4|https://github.com/apple-oss-distributions/objc4.git|objc4-818.2|
libunwind|https://github.com/apple-oss-distributions/libunwind.git|libunwind-200.10|
Libinfo|git@github.com:anurodhp/Libinfo.git|iokit-port||https://github.com/apple-oss-distributions/Libinfo.git
removefile|https://github.com/apple-oss-distributions/removefile.git|removefile-49.40.3|
copyfile|https://github.com/apple-oss-distributions/copyfile.git|copyfile-173.40.2|
Csu|git@github.com:anurodhp/Csu.git|bcm2837-bringup||https://github.com/apple-oss-distributions/Csu.git
bash|git@github.com:anurodhp/bash.git|iokit-port||https://github.com/apple-oss-distributions/bash.git
shell_cmds|https://github.com/apple-oss-distributions/shell_cmds.git|shell_cmds-216.60.1|
file_cmds|https://github.com/apple-oss-distributions/file_cmds.git|file_cmds-321.40.3|
text_cmds|https://github.com/apple-oss-distributions/text_cmds.git|text_cmds-106|
text_cmds_bintrans|https://github.com/apple-oss-distributions/text_cmds.git|text_cmds-165.0.4|
adv_cmds|https://github.com/apple-oss-distributions/adv_cmds.git|adv_cmds-176|
system_cmds|https://github.com/apple-oss-distributions/system_cmds.git|system_cmds-880.60.2|
top|git@github.com:anurodhp/top.git|iokit-port||https://github.com/apple-oss-distributions/top.git
basic_cmds|https://github.com/apple-oss-distributions/basic_cmds.git|basic_cmds-55|
misc_cmds|https://github.com/apple-oss-distributions/misc_cmds.git|misc_cmds-34|
gnudiff|https://github.com/apple-oss-distributions/gnudiff.git|gnudiff-25.40.1|
less|https://github.com/apple-oss-distributions/less.git|less-34|
libarchive|https://github.com/apple-oss-distributions/libarchive.git|libarchive-83.40.4|
CommonCrypto|https://github.com/apple-oss-distributions/CommonCrypto.git|CommonCrypto-60178.40.2|
network_cmds|https://github.com/apple-oss-distributions/network_cmds.git|network_cmds-606.40.2|
cron|git@github.com:anurodhp/cron.git|iokit-port||https://github.com/apple-oss-distributions/cron.git
syslog|https://github.com/apple-oss-distributions/syslog.git|syslog-385|
libutil|https://github.com/apple-oss-distributions/libutil.git|libutil-58.40.2|
ncurses|https://github.com/apple-oss-distributions/ncurses.git|ncurses-57|
libedit|https://github.com/apple-oss-distributions/libedit.git|libedit-55|
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
BSM|git@github.com:anurodhp/bsm.git|iokit-port||https://github.com/apple-oss-distributions/BSM.git
ICU|git@github.com:anurodhp/ICU.git|iokit-port||https://github.com/apple-oss-distributions/ICU.git
libxml2|https://github.com/apple-oss-distributions/libxml2.git|libxml2-34.9|
cctools|https://github.com/apple-oss-distributions/cctools.git|cctools-973.0.1|
Libm|https://github.com/apple-oss-distributions/Libm.git|Libm-2026|
bootp|https://github.com/apple-oss-distributions/bootp.git|bootp-413.80.1|
configd|git@github.com:anurodhp/configd.git|iokit-port||https://github.com/apple-oss-distributions/configd.git
PowerManagement|https://github.com/apple-oss-distributions/PowerManagement.git|PowerManagement-1132.81.1|
chrony|git@github.com:anurodhp/chrony.git|iokit-port||https://github.com/mlichvar/chrony.git
vim|https://github.com/apple-oss-distributions/vim.git|vim-91.80.1|
xorgproto|https://gitlab.freedesktop.org/xorg/proto/xorgproto.git|xorgproto-2025.1|
xtrans|https://gitlab.freedesktop.org/xorg/lib/libxtrans.git|xtrans-1.6.0|
xcb-proto|https://gitlab.freedesktop.org/xorg/proto/xcbproto.git|xcb-proto-1.17.0|
libXau|https://gitlab.freedesktop.org/xorg/lib/libxau.git|libXau-1.0.12|
libXdmcp|https://gitlab.freedesktop.org/xorg/lib/libxdmcp.git|libXdmcp-1.1.5|
libxcb|https://gitlab.freedesktop.org/xorg/lib/libxcb.git|libxcb-1.17.0|
xcb-util|https://gitlab.freedesktop.org/xorg/lib/libxcb-util.git|xcb-util-0.4.1|
xcb-util-wm|https://gitlab.freedesktop.org/xorg/lib/libxcb-wm.git|xcb-util-wm-0.4.2|
xcb-util-renderutil|https://gitlab.freedesktop.org/xorg/lib/libxcb-render-util.git|xcb-util-renderutil-0.3.10|
xcb-util-image|https://gitlab.freedesktop.org/xorg/lib/libxcb-image.git|xcb-util-image-0.4.1|
xcb-util-cursor|https://gitlab.freedesktop.org/xorg/lib/libxcb-cursor.git|xcb-util-cursor-0.1.6|
libX11|https://gitlab.freedesktop.org/xorg/lib/libx11.git|libX11-1.8.13|
libXext|https://gitlab.freedesktop.org/xorg/lib/libxext.git|libXext-1.3.7|
libXrender|https://gitlab.freedesktop.org/xorg/lib/libxrender.git|libXrender-0.9.12|
libXfixes|https://gitlab.freedesktop.org/xorg/lib/libxfixes.git|libXfixes-6.0.2|
libXrandr|https://gitlab.freedesktop.org/xorg/lib/libxrandr.git|libXrandr-1.5.5|
libXcursor|https://gitlab.freedesktop.org/xorg/lib/libxcursor.git|libXcursor-1.2.3|
libICE|https://gitlab.freedesktop.org/xorg/lib/libice.git|libICE-1.1.2|
libSM|https://gitlab.freedesktop.org/xorg/lib/libsm.git|libSM-1.2.6|
libXt|https://gitlab.freedesktop.org/xorg/lib/libxt.git|libXt-1.3.1|
libXmu|https://gitlab.freedesktop.org/xorg/lib/libxmu.git|libXmu-1.3.1|
libXft|https://gitlab.freedesktop.org/xorg/lib/libxft.git|libXft-2.3.9|
zlib|https://github.com/apple-oss-distributions/zlib.git|zlib-76|
expat|https://github.com/apple-oss-distributions/expat.git|expat-26|
freetype|https://gitlab.freedesktop.org/freetype/freetype.git|VER-2-14-3|
fontconfig|https://gitlab.freedesktop.org/fontconfig/fontconfig.git|2.17.1|
libpng|https://github.com/pnggroup/libpng.git|v1.6.58|
pixman|https://gitlab.freedesktop.org/pixman/pixman.git|pixman-0.46.4|
cairo|https://gitlab.freedesktop.org/cairo/cairo.git|1.18.4|
mkfontscale|https://gitlab.freedesktop.org/xorg/app/mkfontscale.git|mkfontscale-1.2.3|
libfontenc|https://gitlab.freedesktop.org/xorg/lib/libfontenc.git|libfontenc-1.1.9|
xserver|https://gitlab.freedesktop.org/xorg/xserver.git|xorg-server-21.1.24|
mesa|https://gitlab.freedesktop.org/mesa/mesa.git|mesa-25.2.8|
libXfont2|https://gitlab.freedesktop.org/xorg/lib/libxfont.git|libXfont2-2.0.7|
libxkbfile|https://gitlab.freedesktop.org/xorg/lib/libxkbfile.git|libxkbfile-1.1.3|
xkbcomp|https://gitlab.freedesktop.org/xorg/app/xkbcomp.git|xkbcomp-1.5.0|
xkeyboard-config|https://gitlab.freedesktop.org/xkeyboard-config/xkeyboard-config.git|xkeyboard-config-2.47|
xdpyinfo|https://gitlab.freedesktop.org/xorg/app/xdpyinfo.git|xdpyinfo-1.4.0|
xwd|https://gitlab.freedesktop.org/xorg/app/xwd.git|xwd-1.0.9|
xev|https://gitlab.freedesktop.org/xorg/app/xev.git|xev-1.2.6|
libXi|https://gitlab.freedesktop.org/xorg/lib/libxi.git|libXi-1.8.2|
libXtst|https://gitlab.freedesktop.org/xorg/lib/libxtst.git|libXtst-1.2.5|
libXpm|https://gitlab.freedesktop.org/xorg/lib/libxpm.git|libXpm-3.5.19|
libXaw|https://gitlab.freedesktop.org/xorg/lib/libxaw.git|libXaw-1.0.16|
xdm|https://gitlab.freedesktop.org/xorg/app/xdm.git|xdm-1.1.17|
libxcvt|https://gitlab.freedesktop.org/xorg/lib/libxcvt.git|libxcvt-0.1.3|
wmaker|git@github.com:anurodhp/wmaker.git|iokit-port|
libffi|https://github.com/apple-oss-distributions/libffi.git|libffi-27|
gnustep-make|git@github.com:anurodhp/tools-make.git|iokit-port||https://github.com/gnustep/tools-make.git
gnustep-base|git@github.com:anurodhp/libs-base.git|iokit-port||https://github.com/gnustep/libs-base.git
gnustep-gui|git@github.com:anurodhp/libs-gui.git|iokit-port||https://github.com/gnustep/libs-gui.git
gnustep-back|https://github.com/gnustep/libs-back.git|back-0_32_0|
gnustep-examples|git@github.com:anurodhp/tests-examples.git|iokit-port||https://github.com/gnustep/tests-examples.git
libtiff|https://gitlab.com/libtiff/libtiff.git|v4.7.1|
gnustep-gap|git@github.com:anurodhp/gap.git|iokit-port||https://github.com/gnustep/gap.git
gnustep-gworkspace|git@github.com:anurodhp/apps-gworkspace.git|iokit-port||https://github.com/gnustep/apps-gworkspace.git
gnustep-eau-theme|git@github.com:anurodhp/gershwin-eau-theme.git|iokit-port||https://github.com/gershwin-desktop/gershwin-eau-theme.git
gershwin-windowmanager|git@github.com:anurodhp/gershwin-windowmanager.git|iokit-port||https://github.com/gershwin-desktop/gershwin-windowmanager.git
'

echo "$REPOS" | while IFS='|' read -r name url ref patch upstream_url; do
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
        echo "error: $name names a patch file ($patch). Patch files are retired;" >&2
        echo "       commit the change to our fork of $name instead (see the header)." >&2
        exit 1
    fi

    # Optional 5th field: real upstream URL for an owned-fork entry (see
    # DAR-214/215/216's "stand up an owned fork" pattern) -- lets a
    # future sync be `git fetch upstream <ref> && git push origin <ref>`
    # with no local-work entanglement, same reasoning as the hand-written
    # swift-corelibs-foundation/PureDarwin blocks below.
    if [ -n "$upstream_url" ]; then
        git -C "$dest" remote add upstream "$upstream_url"
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
# project already gets). Originally tracked PureDarwin/PureDarwin's own
# `next` branch directly (their active-development branch -- `main` is
# frozen releases per their own README), no tag, matching this project's
# llvm-project precedent for "no meaningful tag to pin, track a specific
# real commit's tree instead."
#
# 2026-09-04 (DAR-215): cloned from this project's own fork
# (git@github.com:anurodhp/PureDarwin.git, branch iokit-launchd-port)
# instead of upstream + a `git am` patch-file step -- this is the
# highest-churn repo in the whole gitrepo-tagged batch (launchd/XPC is
# this project's most actively-edited source), and the patch-file
# approach was already falling behind real local history (13 real local
# commits vs. the patch file's 12, one -- DAR-219 -- not yet exported)
# before this fix. Those commits now live as real committed history on
# iokit-launchd-port. third_party-patches/PureDarwin.patch is no longer
# read by this script -- kept in the tree only as a historical record.
# The fork's own `next` branch is left as a clean, unmodified mirror of
# real upstream's `next` for future syncing (`git fetch upstream next &&
# git push origin next`) -- `upstream` remote added below for exactly
# that.
PUREDARWIN_REF="iokit-launchd-port"
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
    echo "+ sparse-cloning PureDarwin @ $PUREDARWIN_REF (own fork, src/Libraries/XPC, CrashReporterClient, cmake only)"
    git clone --filter=blob:none --no-checkout --depth 1 --branch "$PUREDARWIN_REF" \
        git@github.com:anurodhp/PureDarwin.git "$PUREDARWIN_DEST"
    git -C "$PUREDARWIN_DEST" sparse-checkout init --cone
    git -C "$PUREDARWIN_DEST" sparse-checkout set src/Libraries/XPC src/Libraries/CrashReporterClient cmake
    git -C "$PUREDARWIN_DEST" checkout "$PUREDARWIN_REF"
    git -C "$PUREDARWIN_DEST" remote add upstream https://github.com/PureDarwin/PureDarwin.git
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
# own real requirement too. Originally pinned to the closest real tag to
# macOS 11.2 (Feb 2021): swift-5.4-DEVELOPMENT-SNAPSHOT-2021-02-12-a.
#
# 2026-09-04 (DAR-216): cloned from this project's own fork
# (git@github.com:anurodhp/swift-corelibs-foundation.git, branch
# corefoundation-port) instead of upstream + a `git am` patch-file step
# -- the same real fixes (CFPriv.h/CFXMLInterface.c bool/Boolean
# prototype conflicts, CFPreferences.c/CFXMLPreferencesDomain.c
# TARGET_OS guard fixes) now live as real committed history on that
# branch, built on top of the same tag above. third_party-patches/
# swift-corelibs-foundation.patch is no longer read by this script --
# kept in the tree only as a historical record of what corefoundation-
# port's first 3 commits contain. The fork's own `main` branch is left
# as a clean, unmodified mirror of real upstream for future syncing
# (`git fetch upstream <ref> && git push origin <ref>`) -- `upstream`
# remote added below for exactly that. Sparse-checkout only
# CoreFoundation/ -- a plain clone would pull the much larger
# Foundation/TestFoundation/Sources trees this project doesn't need at
# all.
SCF_REF="corefoundation-port"
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
    echo "+ sparse-cloning swift-corelibs-foundation @ $SCF_REF (own fork, CoreFoundation/ only)"
    git clone --filter=blob:none --no-checkout --depth 1 --branch "$SCF_REF" \
        git@github.com:anurodhp/swift-corelibs-foundation.git "$SCF_DEST"
    git -C "$SCF_DEST" sparse-checkout init --cone
    git -C "$SCF_DEST" sparse-checkout set CoreFoundation
    git -C "$SCF_DEST" checkout "$SCF_REF"
    git -C "$SCF_DEST" remote add upstream https://github.com/apple/swift-corelibs-foundation.git
fi

# freebsd-src: real AES core + real MD5/SHA-384/512 bodies to close the
# corecrypto AES gap (docs/backlog.md item 0a) and replace this project's
# from-scratch MD5/SHA-384/512 (docs/backlog.md item 0b's sibling gaps).
# corecrypto itself is confirmed NOT in apple-oss-distributions (searched,
# zero repos, see backlog) -- FreeBSD is this project's documented fallback
# source, ahead of hand-rolling. Non-cone sparse-checkout (rather than this
# project's usual --cone) because the two files this needs out of
# sys/kern/ and sys/sys/ each sit in directories with ~1500 unrelated
# kernel source files -- cone mode only prunes at directory granularity,
# so a literal file-pattern sparse-checkout is the only way to avoid
# pulling all of sys/kern/ and sys/sys/ onto disk for one file each.
#   - sys/crypto/rijndael/{rijndael.h,rijndael-alg-fst.c,rijndael-api.c,
#     rijndael_local.h}: the actual Rijndael reference implementation
#     (Rijmen/Bosselaers/Barreto -- the AES designers themselves),
#     explicitly placed in the public domain by its authors -- the
#     cleanest license of any option investigated, cleaner than the
#     PureDarwin/corecrypto AES core scoped-but-not-used 2026-08-27 (that
#     one had three conflicting license attributions in its own repo).
#     rijndael-api-fst.c/.h and test00.c are a padding-oriented API this
#     project doesn't use -- not pulled.
#   - sys/crypto/sha2/sha512c.c (+ sha512.h/sha384.h/sha512t.h/
#     sha512c_impl.h): Colin Percival / Allan Jude, 2-clause BSD, to
#     replace the from-scratch SHA-384/512 compress loop in
#     corecrypto_sha2.c. sha256c.c/sha256.h/sha224.h/sha256c_impl.h are
#     pulled incidentally by the directory-level rijndael pattern's sibling
#     but NOT used -- SHA-256 is already real corecrypto (backlog item 0b).
#   - sys/kern/md5c.c + sys/sys/md5.h: the RSA Data Security reference MD5
#     (RSA's original 1991-2 license, permissive/attribution-only, the
#     same lineage every other BSD/Apple MD5 traces to), to replace the
#     from-scratch MD5 in corecrypto_md5.c.
#   - lib/libutil/pw_util.c + lib/libutil/flopen.c (DAR-174, chpass(1)):
#     the pinned Apple system_cmds/chpass.tproj/chpass.c's own real,
#     unmodified non-OPEN_DIRECTORY branch calls pw_init(NULL,NULL)/
#     pw_fini()/pw_tempname()/pw_equal()/pw_tmp()/pw_mkdb()/pw_edit()/
#     pw_make()/pw_dup(), none of which are defined anywhere in this
#     checkout (confirmed via exhaustive grep) -- Apple's own build
#     always defines OPEN_DIRECTORY, so this branch was apparently never
#     build-tested by Apple either. Real Apple system_cmds/vipw.tproj/
#     pw_util.c (already built, see build_vipw.sh) is NOT a substitute:
#     its pw_init() takes zero arguments (chpass calls the real BSD
#     pw_init(dir, master) two-arg form) and it lacks pw_dup/pw_equal/
#     pw_fini/pw_make/pw_tempname entirely. FreeBSD's lib/libutil/
#     pw_util.c is the real, current, complete implementation of the
#     shared password-program helper API BSD/Darwin's chpass/vipw/passwd
#     all trace to (its own header: "This file is used by all the
#     'password' programs; vipw(8), chpass(1), and passwd(1)."),
#     confirmed via a real `git ls-tree` against this same pinned
#     release/13.2.0 tag -- pulled here rather than any newer tag so
#     this project has exactly one FreeBSD pin to keep in sync, not two.
#     flopen.c is pw_lock()'s own real dependency (reliable open+flock
#     helper), also lib/libutil, also real/unmodified upstream.
#     lib/libc/gen/pw_scan.c + lib/libc/gen/pw_scan.h: chpass.c's own
#     real non-OPEN_DIRECTORY LOADENTRY branch (`chpass -a`) and edit.c's
#     own real verify() need __pw_scan()/_PWSCAN_WARN/_PWSCAN_MASTER --
#     a DIFFERENT, incompatible real BSD pw_scan lineage than this
#     project's own system_cmds/pwd_mkdb.tproj/pw_scan.c (3-arg
#     `pw_scan(char*, struct passwd*, int*)` there vs. this file's
#     `__pw_scan(char*, struct passwd*, int)`) -- see
#     tools/userland_staging/chpass_pw_scan_freebsd.c's own header for
#     the full citation.
#     tools/userland_staging/chpass_pw_util_freebsd.c adapts pw_util.c
#     (real function bodies copied verbatim, header cites exact line
#     ranges) -- pw_copy()/pw_make_v7()/the pw_initpwd()/pw_scan()
#     wrapper tail are dropped, not because they're broken, but because
#     chpass.tproj already ships its OWN real pw_copy.c (different,
#     3-arg signature -- linking both would be a duplicate symbol) and
#     its own real pw_scan.h/pwd_mkdb.tproj/pw_scan.c pairing (same
#     conflict). See that file's own header for the full real citation.
#   - usr.sbin/pw/* (DAR-341): FreeBSD's real pw(8) -- the actual C
#     account-management program `adduser.pl` itself shells out to.
#     Apple never shipped a plain "adduser"/account-creation CLI
#     (apple-oss-distributions has none; real macOS goes through
#     dscl/sysadminctl/Directory Services, which this project
#     deliberately doesn't have -- flat-file auth via Libinfo, no
#     opendirectoryd). FreeBSD base's own adduser is a Perl script this
#     project has no runtime for; pw(8) is the real underlying program,
#     same "prefer the real implementation over an unavailable wrapper"
#     call as everything else pulled from this checkout. pw_nis.c (YP/NIS
#     support) is pulled with the rest of the directory and DOES build
#     here -- checked its real body directly: it has zero real ypclnt/RPC
#     calls, it's a plain-file passwd-format rewrite triggered only by an
#     explicit `-Y`/`-y` flag, so there's no actual NIS/network
#     dependency to close.
#   - lib/libutil/login_cap.c + login_cap.h (DAR-341): pw_user.c's own
#     real, unmodified `login_getpwclass()`/`login_close()` calls (3
#     call sites, confirmed via grep) -- narrow, real, needed regardless
#     of format support below. Depends only on cgetent()/cgetstr()/
#     cgetnum()/cgetcap()/cgetclose() (real Apple Libc/gen/FreeBSD/
#     getcap.c, matching real BSD signatures, not yet compiled into any
#     shipped dylib -- compiled directly into build_pw.sh like pw_scan.c
#     already is for chpass). login_class.c (setusercontext() et al.,
#     sys/cpuset.h + sys/mac.h + sys/rtprio.h -- none of which exist on
#     Darwin/XNU) is NOT pulled -- pw(8) never calls anything from it.
#   - lib/libutil/login_crypt.c (DAR-341): real, unmodified upstream body
#     of `login_setcryptfmt()`, pw_user.c's other real login_cap.h call
#     (3 sites) -- calls real `crypt_set_format(cipher)`. This project's
#     real crypt(3) (Libc/gen/crypt.c, already used project-wide by
#     login/passwd/chpass/sshd) is Apple's own single-format DES-only
#     implementation with no crypt_set_format() at all -- real FreeBSD
#     lib/libcrypt/crypt.c's own crypt_set_format(format) (checked
#     directly) just linear-scans a registered {name,func} table and
#     returns 1 only on an exact case-insensitive name match, 0
#     otherwise. tools/userland_staging/pw_crypt_set_format_compat.c
#     implements that exact real contract restricted to the one format
#     this project's crypt(3) actually has ("des") -- see that file's
#     own header. Real upstream pw_user.c already treats
#     login_setcryptfmt()==NULL as non-fatal (`warn(...)`, not exit) and
#     falls through to a plain `crypt(password, salt)` call either way,
#     so this keeps every pw(8)-created account's hash in the same
#     DES-crypt format every other account-management tool in this
#     project already reads/writes -- not a behavior downgrade, a
#     documented real capability limit of this project's crypt(3).
#   - lib/libutil/gr_util.c (DAR-341): the real, self-contained group-side
#     counterpart to pw_util.c -- gr_init/gr_lock/gr_tmp/gr_copy/gr_mkdb/
#     gr_fini/gr_equal/gr_make/gr_dup/gr_add/gr_scan, every one of which
#     grupd.c/pw_group.c/pw_vpw.c/pw_user.c call and none of which exist
#     anywhere in this checkout (same real gap class as pw_util.c, just
#     never hit until this ticket since nothing built here touched
#     /etc/group management before). No separate gr_scan.c dependency
#     unlike the pw side -- real upstream's own __gr_scan()/gr_scan() live
#     directly inside this one file.
#   - lib/libutil/_secure_path.c (DAR-341): real, self-contained
#     `_secure_path(path, uid, gid)` -- login_cap.c's own real
#     login_getclass()/login_getpwclass() call this (checks login.conf
#     isn't world-writable / wrong-owned before trusting it), not defined
#     anywhere in this checkout.
#   - usr.bin/su/* : real BSD su(1), pulled alongside pw(8) since both are
#     FreeBSD's own real account/session tools at this same tag -- not
#     yet built (no build_su.sh exists yet), tracked here so a fresh
#     clone's checkout matches what later su(1) work will need instead of
#     silently depending on an unrecorded local sparse-checkout add.
#   - lib/msun/src/* (DAR-384): FreeBSD's msun, the fdlibm-derived libm
#     Apple's own Libm C sources come from. Apple's pinned Libm-2026 has
#     many functions only as ARM32 assembly (Source/ARM/*.s), so
#     build_libm_dylib.sh compiles the msun files for the ones this
#     project's libsystem_m.dylib lacked (ceilf/sinf/cosf/frexp/hypot for
#     cairo). An existing checkout does not pick this path up on its own
#     (this script skips existing dirs): add `/lib/msun/src/*` to
#     third_party/freebsd-src/.git/info/sparse-checkout and run
#     `git -C third_party/freebsd-src read-tree -mu HEAD`, or re-clone
#     with --force.
# Pinned to release/13.2.0 (2023-04), a real tagged FreeBSD release.
FREEBSD_REF="release/13.2.0"
FREEBSD_DEST="third_party/freebsd-src"
if [ -d "$FREEBSD_DEST" ]; then
    if [ "$FORCE" = "1" ]; then
        echo "+ removing existing $FREEBSD_DEST (--force)"
        rm -rf "$FREEBSD_DEST"
    else
        echo "= $FREEBSD_DEST already exists, skipping (use --force to re-clone)"
        FREEBSD_DEST=""
    fi
fi
if [ -n "$FREEBSD_DEST" ]; then
    echo "+ sparse-cloning freebsd-src @ $FREEBSD_REF (rijndael, sha2, md5c.c/md5.h, lib/libutil pw_util.c+flopen.c+login_cap.c+.h+login_crypt.c+gr_util.c+_secure_path.c, lib/libc/gen pw_scan.c+.h, crypto/openssh + secure/ ssh build glue, usr.sbin/pw, usr.bin/su, lib/msun/src)"
    git clone --filter=blob:none --no-checkout --depth 1 --branch "$FREEBSD_REF" \
        https://github.com/freebsd/freebsd-src.git "$FREEBSD_DEST"
    git -C "$FREEBSD_DEST" sparse-checkout init --no-cone
    printf '/sys/crypto/rijndael/*\n/sys/crypto/sha2/*\n/sys/kern/md5c.c\n/sys/sys/md5.h\n/lib/libutil/pw_util.c\n/lib/libutil/flopen.c\n/lib/libc/gen/pw_scan.c\n/lib/libc/gen/pw_scan.h\n/lib/libutil/login_cap.c\n/lib/libutil/login_cap.h\n/lib/libutil/login_crypt.c\n/lib/libutil/gr_util.c\n/lib/libutil/_secure_path.c\n/crypto/openssh/*\n/secure/ssh.mk\n/secure/lib/libssh/*\n/secure/usr.sbin/sshd/*\n/secure/usr.bin/ssh/*\n/secure/usr.bin/ssh-keygen/*\n/usr.sbin/pw/*\n/usr.bin/su/*\n/lib/msun/src/*\n' \
        > "$FREEBSD_DEST/.git/info/sparse-checkout"
    git -C "$FREEBSD_DEST" checkout "$FREEBSD_REF"
fi

# sudo (DAR-340): unlike every other command this project has ported,
# sudo is in NEITHER apple-oss-distributions (never open-sourced by
# Apple, confirmed -- this project's own docs/apple-oss-catalog.md has
# no entry) NOR FreeBSD base (ships only as a ports package, not
# usr.bin/usr.sbin, confirmed absent from this project's own pinned
# release/13.2.0 freebsd-src checkout). Per this project's source-
# priority rule (CLAUDE.md, tier 3 -- "another real upstream open-
# source project Apple's own implementation is downstream of or
# equivalent to"), the real, correct source is the actual sudo project
# itself: https://github.com/sudo-project/sudo.
# Pinned to v1.9.17p2 (DAR-340 review H2, 2026-09-29). The first draft
# pinned v1.9.5p2 to era-match Darwin 20.3, but that is the WRONG
# principle for a setuid-root security tool: 1.9.5p2 predates several
# upstream security fixes (see the 1.9.x NEWS file: e.g. CVE-2021-3156
# is fixed in 1.9.5p2 itself, but later releases fix sudoedit
# extra-argument handling, log-escape and -h/--host issues). Newest
# tag of the newest release series verified with
# `git ls-remote --tags https://github.com/sudo-project/sudo.git`.
# Real scoping already done (2026-09-21, DAR-340's own ticket) so the
# next session doesn't re-derive it:
#   - plugins/sudoers/gram.y+gram.c and toke.l+toke.c: BOTH the real
#     yacc/lex grammar sources AND their real pre-generated C output
#     ship directly in this tag's git tree (confirmed via `git
#     ls-tree`) -- no bison/flex toolchain dependency needed at all,
#     removing what DAR-340's own scoping pass flagged as the single
#     biggest unknown.
#   - PAM is NOT a hard dependency: configure.ac's own `--with-pam` is
#     opt-in (confirmed via grep -- only enabled if explicitly
#     requested AND real PAM dev headers are found), and
#     plugins/sudoers/auth/passwd.c is the real, plain getpwnam()+
#     crypt(3) fallback backend used when no other auth method is
#     configured -- the exact same real algorithm this project's
#     su_pam_unix_static.c (DAR-339) and login.c's own non-PAM branch
#     already use. Building with PAM support left off decouples this
#     port from DAR-339's static-PAM-module machinery entirely.
#   - config.h still needs real, hand-derived generation (same shape
#     as the OpenSSH port's own config.h, DAR-183) -- no reference
#     config.h to diff against since Apple's own sudo build is closed
#     and was never based on this same configure.ac anyway.
SUDO_REF="v1.9.17p2"
SUDO_DEST="third_party/sudo"
if [ -d "$SUDO_DEST" ]; then
    if [ "$FORCE" = "1" ]; then
        echo "+ removing existing $SUDO_DEST (--force)"
        rm -rf "$SUDO_DEST"
    else
        echo "= $SUDO_DEST already exists, skipping (use --force to re-clone)"
        SUDO_DEST=""
    fi
fi
if [ -n "$SUDO_DEST" ]; then
    echo "+ cloning sudo @ $SUDO_REF (real sudo-project/sudo upstream, not Apple/FreeBSD -- see this script's own comment above)"
    git clone --filter=blob:none --depth 1 --branch "$SUDO_REF" \
        https://github.com/sudo-project/sudo.git "$SUDO_DEST"
fi

# DejaVu fonts 2.37 (DAR-384): the DejaVu project's own TTF release
# tarball for tag version_2_37 (see the font/2D stack note above the REPOS
# list for why not the git tag). Verified by SHA-256 before unpacking.
DEJAVU_VER="2.37"
DEJAVU_SHA256="fa9ca4d13871dd122f61258a80d01751d603b4d3ee14095d65453b4e846e17d7"
DEJAVU_DEST="third_party/dejavu-fonts-ttf-$DEJAVU_VER"
if [ -d "$DEJAVU_DEST" ]; then
    if [ "$FORCE" = "1" ]; then
        echo "+ removing existing $DEJAVU_DEST (--force)"
        rm -rf "$DEJAVU_DEST"
    else
        echo "= $DEJAVU_DEST already exists, skipping (use --force to re-fetch)"
        DEJAVU_DEST=""
    fi
fi
if [ -n "$DEJAVU_DEST" ]; then
    echo "+ fetching dejavu-fonts-ttf-$DEJAVU_VER.tar.bz2"
    DEJAVU_TMP="$(mktemp -d)"
    curl -fsSL -o "$DEJAVU_TMP/dejavu.tar.bz2" \
        "https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_${DEJAVU_VER//./_}/dejavu-fonts-ttf-$DEJAVU_VER.tar.bz2"
    got="$(shasum -a 256 "$DEJAVU_TMP/dejavu.tar.bz2" | awk '{print $1}')"
    if [ "$got" != "$DEJAVU_SHA256" ]; then
        echo "error: dejavu-fonts-ttf-$DEJAVU_VER.tar.bz2 SHA-256 is $got, expected $DEJAVU_SHA256" >&2
        rm -rf "$DEJAVU_TMP"
        exit 1
    fi
    tar -xjf "$DEJAVU_TMP/dejavu.tar.bz2" -C third_party
    rm -rf "$DEJAVU_TMP"
fi

# Inter 4.0 (UI font, Mac-like look): the Inter project's own release zip
# (rsms/inter v4.0). The git tag holds only sources and WOFF2 web fonts, and
# FreeType here has no WOFF2 support, so the release zip's static TTFs
# (extras/ttf/) are used. OFL-1.1 licensed (LICENSE.txt in the zip).
# Verified by SHA-256 before unpacking; build_fonts.sh stages the TTFs.
INTER_VER="4.0"
INTER_SHA256="ff970a5d4561a04f102a7cb781adbd6ac4e9b6c460914c7a101f15acb7f7d1a4"
INTER_DEST="third_party/inter-fonts-$INTER_VER"
if [ -d "$INTER_DEST" ]; then
    if [ "$FORCE" = "1" ]; then
        echo "+ removing existing $INTER_DEST (--force)"
        rm -rf "$INTER_DEST"
    else
        echo "= $INTER_DEST already exists, skipping (use --force to re-fetch)"
        INTER_DEST=""
    fi
fi
if [ -n "$INTER_DEST" ]; then
    echo "+ fetching Inter-$INTER_VER.zip"
    INTER_TMP="$(mktemp -d)"
    curl -fsSL -o "$INTER_TMP/inter.zip" \
        "https://github.com/rsms/inter/releases/download/v$INTER_VER/Inter-$INTER_VER.zip"
    got="$(shasum -a 256 "$INTER_TMP/inter.zip" | awk '{print $1}')"
    if [ "$got" != "$INTER_SHA256" ]; then
        echo "error: Inter-$INTER_VER.zip SHA-256 is $got, expected $INTER_SHA256" >&2
        rm -rf "$INTER_TMP"
        exit 1
    fi
    mkdir -p "$INTER_DEST"
    unzip -q -o "$INTER_TMP/inter.zip" 'extras/ttf/*' LICENSE.txt -d "$INTER_DEST"
    rm -rf "$INTER_TMP"
fi

# shared-mime-info 2.4 (freedesktop.org MIME database, GPL-2.0-or-later): GIO (GLib, and so WebKit) decides the content type of a
# file by looking it up here; without /usr/share/mime/mime.cache every file is application/octet-stream and, for example,
# WebKit shows a file:// HTML page as nothing at all. The project's own release archive (the git tag has no generated files
# either way); build_shared_mime_info.sh compiles update-mime-database for the host and stages the generated data.
# Verified by SHA-256 before unpacking.
SMI_VER="2.4"
SMI_SHA256="531291d0387eb94e16e775d7e73788d06d2b2fdd8cd2ac6b6b15287593b6a2de"
SMI_DEST="third_party/shared-mime-info-$SMI_VER"
if [ -d "$SMI_DEST" ]; then
    if [ "$FORCE" = "1" ]; then
        echo "+ removing existing $SMI_DEST (--force)"
        rm -rf "$SMI_DEST"
    else
        echo "= $SMI_DEST already exists, skipping (use --force to re-fetch)"
        SMI_DEST=""
    fi
fi
if [ -n "$SMI_DEST" ]; then
    echo "+ fetching shared-mime-info-$SMI_VER.tar.gz"
    SMI_TMP="$(mktemp -d)"
    curl -fsSL -o "$SMI_TMP/smi.tar.gz" \
        "https://gitlab.freedesktop.org/xdg/shared-mime-info/-/archive/$SMI_VER/shared-mime-info-$SMI_VER.tar.gz"
    got="$(shasum -a 256 "$SMI_TMP/smi.tar.gz" | awk '{print $1}')"
    if [ "$got" != "$SMI_SHA256" ]; then
        echo "error: shared-mime-info-$SMI_VER.tar.gz SHA-256 is $got, expected $SMI_SHA256" >&2
        rm -rf "$SMI_TMP"
        exit 1
    fi
    tar -xzf "$SMI_TMP/smi.tar.gz" -C third_party
    rm -rf "$SMI_TMP"
fi

echo "done -- third_party/ populated. See docs/roadmap.md and README.md for what to build next."
