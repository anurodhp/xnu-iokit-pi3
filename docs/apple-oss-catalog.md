# Apple open-source library catalog

Full audit of `apple-oss-distributions` against this project's actual
needs, done 2026-08-24 at the user's explicit request to systematically
inventory what's available before further work, rather than continuing to
pull sources ad hoc per command/phase the way `setup_third_party.sh` has
so far. Method: cloned `distribution-macOS` (the same cross-project
version manifest `setup_third_party.sh`'s own header comment already
relies on) and diffed its `macos-112` tag's full 232-entry tree against
this project's 32 currently-pinned `apple-oss-distributions` repos, then
against the roadmap/backlog's open items. `docs/daemon-roadmap.md` already
covers background daemons in depth (`cron`, `syslogd`, `configd`,
`mDNSResponder`, `PowerManagement`, `launchd`+XPC, etc.) — this file
complements it with everything else: libraries, filesystem tools,
userland commands, and, most importantly, **a correctness audit of pins
already in use**.

## Part 1: pin correctness audit — 2 of 4 mismatches FIXED 2026-08-24

`setup_third_party.sh`'s comments claim every pin is manifest-verified,
"not guessed by date proximity." That claim was only partially true.
Checked all 32 currently-pinned `apple-oss-distributions` repos' exact
tags against `macos-112`'s own pinned commit for each: **28 match, 4
don't.**

**`IOStorageFamily` and `msdosfs` are now re-pinned and fixed** (same
session, right after this audit) — see `setup_third_party.sh`'s own
updated comment block for the full method. Summary: checked out the
manifest-exact tags (`IOStorageFamily-260.40.5`/`msdosfs-435.60.1`),
identified every file in this project's `third_party/xnu-7195` vendored
copy that actually differs from pristine upstream (exactly 4: this
project's own local fixes in `IOMediaBSDClient.cpp`,
`msdosfs_vfsops.c`, `msdosfs_vnops.c`, plus trivial no-op-equivalent
drift in `msdosfs_lookup.c`/`msdosfs_conv.c`), 3-way merged
(`git merge-file`, base=old pristine tag, local=this project's fix,
other=new pristine tag — zero conflicts, each merge isolated exactly
this project's own hunks) so the fixes (IOMediaBSDClient's lazy-
singleton CAS fix for backlog item 12; msdosfs's root-mount
`copyin(NULL)` fix + `VFS_TBLCANMOUNTROOT`; DEBUG-gated diagnostics)
survived onto the new tags intact, then rebuilt the kernel
(`tools/build_xnu_7195.sh`, one new build-break found and fixed: the
newer `IOStorageFamily` tag's `IOBlockStorageDriver.cpp` includes a
genuinely new header, `IOBlockStoragePerfControlExports.h`, not present
at the old tag — vendored it, zero other new/missing files). Full real
pty-driven QEMU boot verified afterward against the current
`qemu/hfs_root_test.img`: real login, HFS+ root mounts and reports real
`df` usage, **both real MBR partitions correctly enumerated as
`disk0s1`/`disk0s2`** (confirms `IOFDiskPartitionScheme`'s parsing
still works against the new source), a real file write/read/delete
round-trip succeeded, and all 77 existing commands still listed in
`/bin` with no regression. `msdosfs`'s own root-mount-specific local
fixes are confirmed byte-present and compiling clean into the new
kernel; the actual msdosfs-as-root runtime path itself (this project's
"older", superseded single-partition test image,
`qemu/fat32_test.dmg`) no longer exists in this checkout and has no
regenerating script, so that specific path was not re-exercised this
pass — flagged here rather than silently assumed fine.

`hfs-556.100.11` and `IOUSBFamily-630.4.5` were deliberately left
untouched (see below for why); everything else already matched.

| Repo | Currently pinned | `macos-112` manifest actually pins | Status |
|---|---|---|---|
| `hfs` | `hfs-556.100.11` | `hfs-556.60.1` | pinned tag is **newer** than the manifest |
| `IOStorageFamily` | `IOStorageFamily-218.1.4` | `IOStorageFamily-260.40.5` | pinned tag is **older** — real version drift |
| `msdosfs` | `msdosfs-229.1.4` | `msdosfs-435.60.1` | pinned tag is **older** — real version drift |
| `IOUSBFamily` | `IOUSBFamily-560.4.2` *(was `630.4.5` until 2026-09-08)* | *(not in the manifest tree at all)* | no Big Sur-era tag exists to compare against; `630.4.5` re-pinned because it ships its core sources **empty** — see the 2026-09-08 addendum |

Everything else (`dyld`, `Libc`, `libpthread`, `libplatform`,
`libdispatch`, `libclosure`, `libmalloc`, `libunwind`, `Libinfo`,
`removefile`, `copyfile`, `Csu`, `bash`, `shell_cmds`, `file_cmds`,
`text_cmds`, `adv_cmds`, `system_cmds`, `basic_cmds`, `misc_cmds`,
`gnudiff`, `less`, `libarchive`, `CommonCrypto`, `network_cmds`, `cron`,
`libutil`, `IONetworkingFamily`) matches the manifest exactly.

**What this means concretely:**

- **`IOStorageFamily-218.1.4` and `msdosfs-229.1.4` are genuinely the
  wrong vintage** for a Darwin 20.3/macOS 11.2 kernel (`xnu-7195.81.3`,
  itself confirmed correct against this same manifest). `260.40.5` and
  `435.60.1` are real tags that exist in both repos and are what
  `macos-112` actually ships. This is exactly the kind of header/struct-
  layout skew this project's own history has repeatedly hit the hard way
  (`CLAUDE.md`: "this project has a long history of 'obvious' fixes
  turning out to be one layer above or below the real bug") — worth
  re-pinning and rebuilding deliberately rather than leaving as a latent
  risk, especially since HFS+/FAT32 are the project's root filesystem and
  boot device.
- **`hfs-556.100.11` being newer than the manifest** is a different
  shape of problem — not necessarily wrong (Apple ships maintenance point
  releases of individual components between OS drops that don't
  retroactively update the manifest tag), but it means whatever
  originally chose `556.100.11` did *not* actually do a manifest lookup
  for it the way the header comment implies, despite pinning to a real,
  presumably-still-Darwin-20.3-compatible tag. Worth a deliberate decision
  (keep `556.100.11` for known reasons, or drop to the manifest's
  `556.60.1` for byte-for-byte manifest fidelity) rather than an
  unexamined assumption either way.
- **`IOUSBFamily` has no Big Sur-era entry in the manifest at all** —
  confirmed not a naming change either (`IOUSBHostFamily` doesn't exist as
  a repo under `apple-oss-distributions`). Real macOS likely moved USB to
  a closed-source successor stack by 11.2 and simply stopped publishing
  this component's manifest linkage, while the repo itself continued
  getting tagged releases independently. `630.4.5` may be the best
  available source regardless, but it isn't and can't be manifest-
  verified the way the others are — this is an honest ceiling on the
  verification method itself, not a pin bug to fix.
  **Superseded 2026-09-08 — it *was* a real pin bug after all, just not
  a manifest one; see the addendum below.**

**Addendum 2026-09-08 (DAR-244) — `IOUSBFamily-630.4.5` publishes its
core sources as zero-byte files; re-pinned to `IOUSBFamily-560.4.2`.**
Found while actually trying to compile IOUSBFamily for the first time
(DAR-243/DAR-244's real `IOUSBController`/`IOUSBDevice`/`IOUSBInterface`/
`IOUSBPipe` port, needed by `AppleUSBCDCECM`). At tag
`IOUSBFamily-630.4.5` — the newest tag the repo has, and the one this
file previously endorsed as "no better tag to move to" — the following
are **literally 0 bytes**, in the real upstream repo and not just in
this project's clone (verified independently via GitHub's contents API
for that ref, which reports `"size": 0` for each):

| Path | `630.4.5` | `560.4.2` |
|---|---|---|
| `IOUSBFamily/Classes/IOUSBController.cpp` | **0** | 170,433 |
| `IOUSBFamily/Classes/IOUSBDevice.cpp` | **0** | 165,616 |
| `IOUSBFamily/Classes/IOUSBInterface.cpp` | **0** | 82,276 |
| `IOUSBFamily/Classes/IOUSBControllerV3.cpp` | **0** | 86,867 |
| `IOUSBFamily/Classes/IOUSBControllerV2.cpp` | **0** | 64,521 |
| `IOUSBFamily/Classes/IOUSBRootHubDevice.cpp` | **0** | 42,063 |
| `IOUSBFamily/Classes/IOUSBPipeV2.cpp` | **0** | 33,848 |
| `IOUSBFamily/Classes/IOUSBControllerUserClient.cpp` | **0** | 10,145 |
| `IOUSBFamily/Classes/IOUSBController_Errata.cpp` | **0** | 12,703 |
| `IOUSBFamily/Classes/IOUSBWorkLoop.cpp` | **0** | 3,481 |
| `IOUSBFamily/Headers/USB.h` | **0** | 59,299 |
| `IOUSBFamily/Headers/IOUSBController.h` | **0** | 54,433 |
| `IOUSBFamily/Headers/IOUSBControllerV3.h` | **0** | 24,646 |
| `IOUSBFamily/Headers/IOUSBLog.h` | **0** | 7,603 |
| `IOUSBFamily/Headers/IOUSBControllerUserClient.h` | **0** | 2,915 |
| `IOUSBFamily/Headers/IOUSBWorkLoop.h` | **0** | 2,007 |
| `IOUSBFamily/Headers/IOUSBPriv.h` | **0** | 1,739 |
| `IOUSBFamily/Headers/IOUSBAppleIDs.h` | **0** | 60 |
| `AppleUSBHub/Classes/AppleUSBHub.cpp` | **0** | *(real)* |
| `AppleUSBHub/Classes/AppleUSBHubPort.cpp` | **0** | *(real)* |
| `AppleUSBHub/Classes/AppleUSBHSHubUserClient.cpp` | **0** | *(real)* |

`560.4.2` (the next-newest tag; the repo has 51 tags total, newest first
`630.4.5`, `560.4.2`, `555.4.4`, …) has **zero** empty files anywhere in
`IOUSBFamily/Classes` or `IOUSBFamily/Headers` — 24,804 real lines of
source across `Classes/` alone. So the correct pin for anyone who wants
to *build* IOUSBFamily rather than skim its headers is `560.4.2`, and
`setup_third_party.sh` now pins that.

**The generalizable lesson**, worth more than the pin itself: "newest
tag" and "newest tag that actually contains the source" are different
things, and this project's own pin-audit method could not tell them
apart, because it only ever compared *tag names* against the manifest.
Nothing caught it for over a month because **nothing ever compiled
IOUSBFamily** — DAR-244 confirms no build script referenced it; it was
used only as reference headers, and the specific headers anyone happened
to read (`IOUSBInterface.h`, `IOUSBDevice.h`, `USBSpec.h`) are among the
ones that are *not* empty. A pin is only trustworthy to the depth
something actually consumes it. Same shape as this file's own 2026-08-24
lesson, one level down: there, a pin was present but never diffed
against the manifest; here, a pin was diffed against the manifest but
its *contents* were never opened.

**Addendum 2026-09-08 (DAR-243/DAR-244) — two new header-only pins added
alongside the IOUSBFamily fix.** Both exist purely so IOUSBFamily and
AppleUSBCDCDriver *parse*; neither describes hardware this board has,
and no source from either is compiled.

| Repo | Pinned | Manifest says | Why |
|---|---|---|---|
| `IOPCIFamily` | `IOPCIFamily-428.80.2` | **matches** — `macos-112` pins commit `b6412a04…`, whose commit message is `IOPCIFamily-428.80.2` | `IOUSBControllerV3.h` declares an `IOPCIDevice *_device`; `IOUSBController.cpp` references `IOPCIDevice` in 8 places |
| `IOSerialFamily` | `IOSerialFamily-93.200.2` | *(not in the manifest tree at all)* | `AppleUSBCDC.cpp` — the CDC master driver both ECM drivers depend on — includes `<IOKit/serial/IOSerialKeys.h>` and three siblings for its ACM path |

`IOPCIFamily` is the better-behaved of the two: it is a real
manifest-verified pin, resolved by reading `distribution-macOS`'s
submodule commit and then that commit's own message, rather than by
picking the newest tag. `IOSerialFamily` has the same "no manifest
entry" ceiling as `IOUSBFamily`/`Libm`/`BSM` — its repo has only three
tags ever (`91`, `93`, `93.200.2`) and `macos-112` does not list it — so
`93.200.2` is simply the newest real tag, documented as such.

Both were checked for the `IOUSBFamily-630.4.5` empty-file failure mode
before being trusted. All six `IOKit/pci` headers and all six
`IOSerialFamily.kmodproj` headers are real, non-empty files. That check
is now a permanent guard rather than a one-off, and it covers **every**
arm of the shim — `network`, `storage`, `usb`, `pci` and `serial`:
`tools/setup_sdk_shim.sh` fails loudly if any header it links resolves
to a zero-byte file, and `tools/build_iousbfamily.sh` refuses to run at
all if `IOUSBController.cpp` is empty — because a wrong pin there does
not fail the build, it silently compiles nothing.

Worth stating plainly, since it is the whole reason the guard is a `-s`
test and not an existence test: **an empty header is strictly worse than
a missing one.** The symlink resolves, the file exists, the `#include`
succeeds, and everything the header should have declared is simply
absent — so the failure surfaces hundreds of lines later as unrelated
"unknown type name" errors, or not at all. `link_headers`' existence
check cannot see it.

Note for the DAR-243 work specifically: `AppleUSBCDCDriver-4303.2.1`
(pinned separately, and complete — no empty files) still `#include`s the
legacy family (`<IOKit/usb/IOUSBDevice.h>`, `IOUSBPipe.h`,
`IOUSBInterface.h`, `IOUSBNub.h`, `IOUSBBus.h`, `USB.h`) and contains
zero references to `IOUSBHostFamily`/`IOUSBHostInterface`/`USBDriverKit`
— checked in its own source rather than inferred from its version
number. So legacy IOUSBFamily really is the right partner for it,
despite `4303.2.1` being a much later-era tag.

**Addendum 2026-08-27 — `Libm` pinned, same "no manifest entry" ceiling as
`IOUSBFamily` but worse:** `setup_third_party.sh` now pins
`apple-oss-distributions/Libm` (never pinned before this project had any
Libm at all). Checked `distribution-macOS`'s `release.json` at `macos-112`
*and* every other macOS tag back through `macos-1012` (Sierra,
10.12–10.15, 11.1, 11.2 all checked directly, not sampled) — `Libm` is
listed in **none** of them (`macos-112` alone has 188 real project
entries, zero named `Libm`). The repo is real and still exists, but its
own tag list is only 15 tags total, and the real tagger dates on those
tag objects (`git cat-file tag <sha>`, since GitHub's own import-commit
dates on these bulk-migrated repos are not reliable era signals) run from
2003 up to `Libm-2026` at **2011-07-20** — Apple has not published Libm
source at all since OS X Lion, a decade before Darwin 20.3/macOS 11.2 and
about two years before arm64 existed as an architecture at all. Pinned
`Libm-2026` anyway (newest real tag, same "pin the newest available real
source, document the ceiling honestly" call as `IOUSBFamily` above), but
unlike `IOUSBFamily` this one also has zero `arm64`/`AArch64` source in
any tag ever published — `Source/ARM` is real armv6/armv7 (32-bit)
assembly plus a handful of portable C fallback files (many already
FreeBSD-derived, vendored by Apple itself under `_freeBSD.c` names).
`tools/userland_staging/build_libm.sh` only builds from the portable C
subset (the assembly is skipped, not ported) and falls back to real,
directly-transcribed FreeBSD `lib/msun` source (`modf`, `trunc`, `fabs`,
`pow`, `scalbn` — attributed in
`tools/userland_staging/libm_freebsd_fallback.c`) for the handful of
functions where Apple's own real Libm source has no usable portable
form (`pow`/`fabs` are ARM-asm-only; `modf`'s only portable Apple source
is its own `modf_IncrediblySkanky.c`, self-described in its own header
comment as a temporary, poor-performance hack).

**Addendum 2026-08-27/28 — `BSM` built (`tools/userland_staging/build_bsm.sh`),
same "no manifest entry" ceiling as `Libm`/`IOUSBFamily`, plus a real
cross-repo API skew found and patched:** `apple-oss-distributions/BSM`
(`bsm-13`) was pinned earlier the same day for Phase 6.8's real, named
`bsm/auditd_lib.h`/`bsm/libbsm.h` launchd/libxpc dependency but never
actually built until this pass — ported proactively ahead of any real
consumer, same spirit as `Libm` above and the user's own explicit ask
("other apple oss libraries like libm that we have not built"). Checked
`distribution-macOS`'s `macos-112` manifest directly (`git cat-file -p
macos-112^{tree}`, all 232 real entries) — no entry named `BSM` or
`audit` exists at all, same real ceiling as `Libm`/`IOUSBFamily`.
`bsm-13` confirmed the newest real tag (`git ls-remote --tags`: `bsm-13`
> `bsm-8` > `bsm-2.11` > `bsm-2.10`), not guessed.

All 11 real files in `third_party/BSM/bsm/lib/*.c` (BSM's own real
Makefile's exact file list) compile clean and archive into a real
`libc_build/libbsm.a` (99 exported functions) — zero files excluded,
unlike `Libm`'s honest partial-exclusion list. Real gaps found and
closed, all documented in `build_bsm.sh`'s own header comment and
`tools/userland_staging/bsm_compat_headers/bsm_internal_compat.h`: a
real, confirmed omission in BSM-13's own `libbsm.h` (`au_groups_t` uses a
bare `MAX_GROUPS` that is defined nowhere in the checkout at all); a real
naming-convention drift between BSM-13's userland lib source and this
project's paired `xnu-7195` `bsd/bsm/audit_internal.h`/`audit_record.h`
(`HEADER_SIZE`/`TRAILER_SIZE`/`AU_*_TOKEN`/`AU_NULL`/`AUR_LONG` vs. the
real, current `AUDIT_HEADER_SIZE`/`AUDIT_TRAILER_SIZE`/`AUT_*`/
`AUT_INVALID`/`AUR_INT32` names — BSM-13's own lib source predates a
later xnu-side rename it was never resynced against); missing
`be16enc`/`be32enc`/`be64enc` (real, standard BSD `<sys/endian.h>`
big-endian-encode API used by xnu-7195's own `ADD_U_INT16/32/64` macros,
not present anywhere in this project's vendored Libc — a real, standard,
unambiguous algorithm transcribed directly rather than pinning a whole
extra repo for 3 one-line functions). A real, minimal **patch to BSM's
own source** (`third_party-patches/BSM.patch`, applied via `git am` like
`Csu.patch`/`Libc.patch`/`ICU.patch`, verified to apply cleanly against a
fresh `bsm-13` checkout): `xnu-7195`'s own `bsd/bsm/audit_record.h`
already declares a full "deprecated userland BSM API" prototype block (9
functions — `au_to_arg`/`data`/`groups`/`opaque`/`file`/`path`/`seq`/
`exec_args`/`exec_env`) with slightly different signatures than what
BSM-13's own `bsm_token.c` defines (mostly a dropped/added `const`
qualifier, `int *` vs. `gid_t *` for `au_to_groups` — xnu's own header is
arguably the buggy side there, since the very next declaration,
`au_to_newgroups`, correctly uses `gid_t *` — plus one genuine behavioral
difference: `au_to_file` gained an extra `struct timeval` parameter in
the newer header that this implementation predates, accepted-and-ignored
since the function already computes its own real timestamp via
`gettimeofday()`). Fixed by aligning BSM-13's 9 definitions to match
xnu-7195's real, currently-published contract — a real, confirmed
cross-repo API-vintage skew between two real Apple sources, not
something this project introduced, same "real, confirmed bug in a
pristine Apple snapshot, patched with a documented `git am` patch"
precedent as `dyld.patch`'s (now-retired) `ImagePair` fix / `ICU.patch`'s
`aaplbfct.cpp` omission.

Real new syscall wrappers (`tools/userland_staging/bsm_syscalls.c`):
`audit(2)`/`auditon(2)`/`getaudit(2)` (#350/#351/#355, cross-checked
against `xnu-7195`'s own `bsd/kern/syscalls.master`). Real, confirmed,
not a project-specific gap: `syscalls.master` lists #355 as `{ int
nosys(void); } { old getaudit }` — this exact `xnu-7195` kernel has
genuinely retired legacy `getaudit(2)` in favor of `getaudit_addr(2)`
(#357, not wrapped — no real BSM-13 lib source calls it), matching real,
current Darwin's own actual behavior, not a simplification.

Real smoketest (`bsm_smoketest.macho`, `nm -u`-verified zero undefined
symbols, `LC_UNIXTHREAD` present, zero `LC_LOAD_DYLIB`): a real
`au_open()`/`au_to_subject32()`/`au_to_return32()`/`au_to_text()`/
`au_write()`/`au_close()` record cycle. Built as a static archive
(`libbsm.a`), not a `.dylib` — real Apple's own `bsm/lib/Makefile`
actually targets a dylib, but this project's own recent precedent for a
curated support library linked into Csu-static consumers (`libm.a`,
`libnotify_client.a`, `libresolv.a`, `libopenpam.a`, all built after the
project's real per-library-dylib convention was announced) is `.a`; not
wired into any consumer yet (no real auditd/notifyd/OpenSSH/launchd
consumer exists on this target either), same "build it in advance, ready
for when something needs it" framing as `Libm`.

**Not yet done, scope for a follow-up pass, not this one:** the same
check for the 5 repos vendored via `third_party-patches/` with local
fixes on top (`dtc-AppleDeviceTree`, `u-boot`, `Csu`) — `Csu` matched
above since it's also a plain `apple-oss-distributions` pin, but
`dtc-AppleDeviceTree`/`u-boot` aren't Apple repos and are out of scope for
this manifest method entirely.

## Part 2: available repos matching an already-documented gap

Everything below already has a named gap, workaround, or stub in
`docs/roadmap.md`/`docs/backlog.md` — i.e. this is "stop working around
it, here's the real source," not speculative scope growth. Tags below are
each confirmed against `macos-112` the same way as Part 1 (not guessed).

| Repo | Tag (macos-112) | Replaces / closes |
|---|---|---|
| [`ncurses`](https://github.com/apple-oss-distributions/ncurses) | `ncurses-57` | Real termcap/terminfo database — closes `docs/backlog.md` P2 item 11's documented gap ("no real termcap/terminfo *database*... `less` always runs in real hardcopy mode"). Bash's own bundled `bash_readline_termcap_compat.c` shim would also become unnecessary. |
| [`gnutar`](https://github.com/apple-oss-distributions/gnutar) | `gnutar-455` | `tar` — `docs/roadmap.md`'s Tier B explicitly lists `tar` as "no real source cloned into this checkout yet." |
| [`diskdev_cmds`](https://github.com/apple-oss-distributions/diskdev_cmds) | `diskdev_cmds-667.40.1` | `newfs_hfs`/`fsck_hfs`/`fstyp`/`mount_hfs` — real Apple HFS+ userland tooling; currently the SD image (`tools/make_sd_image.sh`) is built with host macOS tools, not this project's own ported source. Also the real source for `disklib`, used by `diskutil`-adjacent tooling if ever wanted. |
| [`awk`](https://github.com/apple-oss-distributions/awk) | `awk-27.40.1` | No `awk` exists anywhere in this project (confirmed by grep — not in any `build_*.sh`, not mentioned in roadmap/backlog at all). Common enough that its total absence is itself worth flagging, independent of any documented "gap." |
| [`OpenPAM`](https://github.com/apple-oss-distributions/OpenPAM) | `OpenPAM-28.40.1` | `docs/roadmap.md`'s Phase 5 explicitly names "OpenPAM as fallback" for real PAM, still unbuilt. |
| [`libresolv`](https://github.com/apple-oss-distributions/libresolv) | `libresolv-68` | Phase 7a (DNS) is explicitly scoped as "small and clean once Phase 6 lands" and names `libresolv` by that exact name. |
| [`OpenSSH`](https://github.com/apple-oss-distributions/OpenSSH) | `OpenSSH-240.40.1` | **Not the source of this project's sshd** (2026-09-20, DAR-183). This pin is OpenSSH **8.1p1**; Phase 7b is built from FreeBSD's `crypto/openssh` at the existing `release/13.2.0` pin instead — real OpenSSH **9.3p1** — per DAR-183's own instruction to use a current FreeBSD sshd rather than an old Apple one. This checkout stays pinned and is still used, but only as the reference for Darwin-side `HAVE_*` answers (its real, checked-in Darwin-generated `openssh/config.h`) when `tools/userland_staging/build_sshd.sh` generates its own. The old "blocked on real AES" note is also superseded: that build is `--without-openssl`, so OpenSSH uses its own in-tree crypto and needs nothing from the kernel's `g_crypto_funcs`. |
| [`IOKitUser`](https://github.com/apple-oss-distributions/IOKitUser) | `IOKitUser-1845.81.1` | Userland IOKit (`IOKitLib`) — not on any phase list today, but directly relevant given this project's whole driver stack is IOKit; would unlock a real `ioreg`-equivalent for on-target driver/personality debugging instead of only QEMU-monitor introspection. |
| [`IOKitTools`](https://github.com/apple-oss-distributions/IOKitTools) | `IOKitTools-114.40.1` | Real `ioreg`/`ioclasscount`/`ioalloc` sources, consumes `IOKitUser` above. Same rationale. |
| [`libiconv`](https://github.com/apple-oss-distributions/libiconv) | `libiconv-59` | This project's `libc_locale` work is explicitly documented as "write custom code only as a last resort" — `libiconv` is real, small, and could replace some of the from-scratch locale/charset stand-ins the same session's work built, not yet checked against them. |

**Already scoped in `docs/daemon-roadmap.md`, not re-litigated here** —
cross-referenced for completeness since they showed up in this same
manifest sweep: `syslog` (`syslog-385`), `configd` (`configd-1109.60.2` —
**pinned + cloned into `third_party/configd` 2026-09-01**, tag reverified
against the real `macos-112` manifest JSON at clone time; cloned as the
real source home of `SystemConfiguration.fproj`/`libSystemConfiguration`
that `bootp`'s own headers transitively need, see below — the real daemon
(`configd.tproj`) itself is NOT yet built/ported, only the source is on
disk), `mDNSResponder` (`mDNSResponder-1310.80.1`), `bootp`
(`bootp-413.80.1` — DHCP client/`ipconfigd`; **pinned + cloned into
`third_party/bootp` 2026-09-01**, tag reverified against the real
`macos-112` manifest JSON at clone time, superseding the earlier
"deprioritized" framing from the `project_xnu_rpi3_dhcp_ipconfiguration_
analysis` memory. That memory's conclusion — full `SCDynamicStore`/
`configd` is a real wall, so skip Apple's client and port FreeBSD
`dhclient` instead — undersold Apple's own source: a real per-file
`SCDynamicStore`/`SCPreferences` grep of the pinned tag shows the coupling
is concentrated in `IPConfiguration.bproj/ipconfigd.c` (112 refs, the
daemon main loop/per-service state-machine driver) and `sysconfig.c` (13
refs), while the actual DHCP/BOOTP protocol engine — `dhcp.c`, `bootp.c`,
`bootp_session.c`, `arp_session.c`, `DHCPLease.c`, `ifutil.c` (the ioctl
layer `dhcp.c` calls through `service_set_address()`/
`service_remove_address()`) — has zero `SCDynamicStore` references and is
portable standalone today. A real first build-script pass
(`tools/userland_staging/build_bootp_dhcp_engine.sh`) got 5 of 9 targeted
protocol-engine files compiling clean against real headers (including
real CoreFoundation and, via a staged `-I`, `SystemConfiguration.fproj`'s
own headers pulled from the `configd` clone); the remaining 4 hit one
precisely-diagnosed real gap (`SCPrivate.h` → XNU's kernel-internal
`os/log_private.h`/firehose COMM_PAGE machinery, not a userspace-
appropriate header — same class this project already stubbed out
elsewhere, see that script's own header comment). See Linear DAR-170 for
the full ordered plan this finding produced), `PowerManagement`
(`PowerManagement-1132.81.1` — also the eventual real implementation for
`BCM2837CPU::quiesceCPU()`/`haltCPU()`'s current no-ops, backlog P3 item
4), `bootstrap_cmds` (`bootstrap_cmds-121` — real `mig` source, though this
project currently uses the host Xcode's `/usr/bin/mig` directly and that's
working fine, so no actual need to vendor it).

## Part 3: everything else in the manifest (201 entries total; ~185 after Part 2)

Surveyed, not pinned, and — with the few call-outs below — not worth
pinning for this project's actual goals (a real Darwin userland shell +
IOKit driver stack on bare Pi 3 hardware, no GUI, no enterprise auth, no
scripting-language runtime as a goal in itself):

- **GUI/graphics/WebKit stack** (`WebCore`, `WebKit`, `WebKit2`,
  `WebInspectorUI`, `JavaScriptCore`, `WTF`, `IOAudioFamily`) — this
  target has no display server, no HID input beyond a serial console, no
  audio hardware driver. Structurally inapplicable, not merely
  deprioritized. **`IOGraphics` is no longer in this bucket** — pinned
  2026-09-21 (`IOGraphics-585`, matching the
  `macos-112` manifest exactly) for DAR-331 (real HDMI framebuffer, not
  just the kernel's own boot/panic text console). Real scope there is
  narrow: `IOGraphicsFamily/IOBootFramebuffer.cpp` is a generic "dumb
  framebuffer shim" — it matches on provider property
  `AAPL,boot-display` and just wraps whatever `getPlatform()->
  getConsoleInfo()` already contains, no VideoCore-specific code needed.
  **`IOHIDFamily` left this bucket the same day, for one header only** —
  pinned `IOHIDFamily-1633.81.2`, which is what the `macos-112` manifest
  itself names (`release.json:105-106`), because IOGraphics cannot be
  compiled without it: `IOGraphicsFamily/IOKit/graphics/
  IOFramebufferShared.h:26` includes `<IOKit/hidsystem/IOHIDTypes.h>`
  unconditionally, for the `IOGPoint`/`IOGBounds` cursor-geometry types
  that `StdFBShmem_t` is declared in terms of. Every IOGraphics source
  that reaches `IOFramebuffer.h` stops there with a hard `file not
  found` before any other gap is visible (found by compiling).
  **Revisited 2026-09-24 (DAR-245):** now that HDMI output works, a USB
  keyboard makes the board usable without a serial cable. The real
  kernel path for that is USB HID → `IOUSBHIDDriver` (IOUSBFamily-560.4.2)
  → `IOHIDDevice`/`IOHIDInterface` → `IOHIDEventDriver` → `IOHIDKeyboard`
  legacy shim → `IOHIDSystem` → `cons_cinput()` (IOHIDSystem.cpp:2311-2317).
  So the kernel half of `IOHIDFamily` is no longer "inapplicable": it is
  built into the kernel (`tools/vendor_iohid_into_xnu.sh`, run by
  `build_xnu_7195.sh`), together with IOUSBFamily's `IOUSBHIDDriver` and
  `IOUSBCompositeDriver`. The latter configures a class-0 device so its HID
  interface nub exists at all. QEMU-verified 2026-09-24: `usb-kbd` behind the
  hub types into the shell on both the HDMI console and serial, and the
  same keystrokes do nothing without boot-arg `hid-legacy-shim=1` (Apple's
  own switch for the kernel keyboard shim; `runvm_hdmi.sh` passes it). The
  sources needed source fixes, now commits on the fork
  `anurodhp/IOHIDFamily` (branch `iokit-port`, DAR-356), plus
  `compat/iohid/`;
  `tools/iohid_gap_audit.sh` is the fast compile check. Userspace HID
  (`IOHIDManager`, `hidd`, event-system plugins) stays out of scope:
  there is no window server. Two findings about the drop itself:
  `AppleHIDUsageTables.h` is a license-only stub in every tag from 1090
  to 2238 (the last real one is 1035.70.7), and 1633.81.2 is published
  unifdef'd for macOS (1633.60.4 still has the `TARGET_OS_IPHONE` blocks),
  which is why the keyboard-shim path survives an iPhoneOS-SDK build.
  This project's own `third_party/xnu-7195` already does the hard part:
  `pexpert/arm/pe_init.c`'s `bcm2837_video_init()` already queries the
  VideoCore GPU over the mailbox for a real HDMI framebuffer and
  populates `PE_state.video`/`boot_args->Video` before
  `PE_create_console()` runs — today that's used only to draw the
  kernel's own text console. The real remaining work is wiring
  (`AAPL,boot-display` on the BCM2837 platform-expert nub +
  `IOBootFramebuffer` actually built in), not writing a new driver.
  Confirmed byte-identical to `third_party/PureDarwin`'s `next`-branch
  copy of the same file (`diff -q`, 2026-09-21), so this real Apple pin
  supersedes reading it out of PureDarwin per the source-priority rule
  above. **Corrected later the same day by actually compiling it
  (`tools/iographics_gap_audit.sh`):** "wiring, not a new driver" is
  right about the *design*, but this drop does not build as published.
  `IOBootFramebuffer.cpp` and 7 of the family's other 11 kernel sources
  compile clean against this tree; `IOFramebuffer.cpp`, `GTrace.cpp`,
  `IODisplay.cpp` and `IODisplayWrangler.cpp` do not, because Apple ships
  `.cpp` files from a newer internal revision than the headers beside
  them (missing `clearEvent`/`triggerEvent`, missing
  `IOFramebufferPrivate` members, missing `GTraceBuffer` internals, 24
  missing `DBG_IOG_*` ktrace codes). Checked `IOGraphics-530.66`, `-585`
  and `-600`: same class of gap in all three, so the manifest pin is not
  the problem and there is no complete tag to move to. The source fixes
  are commits on the fork `anurodhp/IOGraphics`, branch `iokit-port`:
  `IOGraphics-585` plus one commit per fix (DAR-355, 2026-09-27). That
  fork is what `setup_third_party.sh` clones. They were previously
  applied in place by the now-retired `tools/patch_iographics_gaps.sh`.
- **Enterprise/network auth & directory services** (`Heimdal`,
  `OpenLDAP`, `KerberosHelper`, `MITKerberosShim`, `Security`,
  `security_certificates`, `security_authtrampoline`, `SecurityTokend`,
  `passwordserver_sasl`, `SmartcardCCID`) — no directory service exists
  on this target at all (per `CLAUDE.md`, no `opendirectoryd`); this
  project's account model is the flat-file resolver already built.
- **Scripting-language runtimes** (`perl`, `python`, `python23`, `ruby`,
  `RubyGems`, `tcl`, `swig`, `pyobjc`) — no interpreter is a project goal;
  would each be their own multi-phase undertaking bigger than most
  existing phases combined, and nothing in the roadmap calls for one.
- **Web/mail server stack** (`apache`, `apache1`, `apache2`, every
  `apache_mod_*`, `postfix`, `mailman`, `cups`) — server daemons for
  services this single-user embedded target has no use case for.
- **Legacy/alternate shells** (`ksh`, `tcsh`, `zsh`, `screen`) — bash is
  already the working default login shell; no stated need for another.
- **Filesystems this target doesn't mount** (`AppleRAID`, `autofs`,
  `cddafs`, `ntfs`, `NFS`, `webdavfs`, `SMBClient`) — HFS+ and msdosfs are
  the only two mounted filesystem types anywhere in this project.
- **Storage/bus families for hardware this board doesn't have**
  (`IOATAFamily`, `IOATAPIProtocolTransport`, `IOBDStorageFamily`,
  `IOCDStorageFamily`, `IODVDStorageFamily`, `IOFireWireFamily` and its
  three siblings, `IOPCIFamily`, `IOSCSIParallelFamily`) — BCM2837 has no
  ATA/SCSI/FireWire/PCI/optical hardware.
- **Build/packaging/release infra for Apple's own internal pipeline**
  (`CoreOSMakefiles`, `pb_makefiles`, `project_makefiles`,
  `bsdmanpages`, `pbx_jamfiles`, `usertemplate`, `release.json`,
  `.gitmodules`) — this project already has its own build scripts; these
  are Apple's Xcode-project scaffolding, not portable build logic.
- **Kext loading/management tooling** (`kext_tools`) — structurally
  inapplicable per `CLAUDE.md`: "the handful of IOKit drivers... are
  compiled directly into the kernel, not loaded as kext bundles."
  `AvailabilityVersions` (SDK versioning macros) is a similar case —
  relevant only to a real SDK-header-generation pipeline this project
  doesn't have.
- **Networking daemons/tools with no current use case**: `bind9` (DNS
  *server*, vs. `libresolv`'s client role above, already actionable),
  `net_snmp`, `ppp`, `eap8021x`, `dcerpc`, `gssd`, `rsync`, `subversion`,
  `curl`, `netcat`/`lsof`/`tcpdump`/`top`/`remote_cmds` (genuinely useful
  diagnostic tools eventually, but no current roadmap item calls for
  them — natural Phase 6c/Tier-C candidates once basic `ping`/`netstat`/
  `arp`/`route` from the already-pinned `network_cmds` land first).
- **`zlib` and `expat` — NOW PINNED** (2026-09-26, DAR-384, font/2D
  stack): `zlib-76` and `expat-26`, exact `macos-112` manifest matches
  (the manifest's submodule commits `805f40328df8...` and
  `37469d253ca9...` are what `git ls-remote` gives for those two tags).
  Built as `/usr/lib/libz.1.dylib` (upstream zlib 1.2.11) and
  `/usr/lib/libexpat.1.dylib` (upstream expat 2.2.8) by
  `build_zlib.sh`/`build_expat.sh`, for libpng/FreeType/cairo and
  fontconfig. u-boot's vendored zlib core is still what the older
  commands (gzip, file, tar) compile in.
- **Misc format/data libraries not currently needed**:
  `libxslt`, `BerkeleyDB`,
  `iodbc`, `libpcap` (only useful once `tcpdump` is wanted), `bzip2`,
  `zip`, `xar`, `hunspell`, `CrackLib`.
- **`objc4` — NOW PINNED** (2026-09-04, `objc4-818.2`, exact `macos-112`
  manifest match, verified via `git rev-list` against the manifest's real
  submodule commit `62b60ba0e56440e265ca576cc9f197e9af54c1bd`). This
  entry's own prior text ("no Objective-C anywhere in this project... 
  nothing currently depends on them") is now stale — DAR-170's real
  `configd.tproj` port found two real Apple plugins genuinely written in
  Objective-C (`IPMonitor`, `QoSMarking`), and the user decided to port a
  real, minimal `objc4` runtime rather than stub or rewrite them, per this
  project's standing "match real Apple even at a big delta" rule. First
  real Objective-C consumer in this project's history.
- **C++ runtime support** (`libcppabi`, `Libcpp_kext`, `libstdcxx`,
  `libstdcxx_SUPanWheat`) — still correctly not needed: the C++ used by
  IOKit drivers already builds and links today without these (confirmed
  — the kernel and kexts build clean), and this project's real
  `libc++`/`libc++abi` (via the pinned LLVM 11.1.0 source) already covers
  userland C++ needs.
- **Other package's own already-superseded internals**: `old_ncurses`
  (superseded by `ncurses` above), `OpenSSL098` (ancient vintage, and
  crypto work here is explicitly scoped through corecrypto/AES instead —
  `docs/backlog.md` P2 item 0a — not OpenSSL).
- **Everything genuinely cosmetic/documentation**: `man`, `doc_cmds`,
  `texinfo`, `groff`, `tidy`.
- **Misc single-purpose utilities with no current caller**: `bc`, `nano`,
  `patch_cmds`/`gpatch` (only needed if this project starts
  consuming patch files at runtime, not currently), `modemccl`, `uucp`,
  `disklabel`/`gpt`/`bless` (bootability tooling for a boot chain this
  project's `u-boot` already handles differently), `system_config`,
  `UserNotification`, `Libnotify` (real dependency is Mach IPC only per
  `docs/daemon-roadmap.md`'s own inventory — filed there, not here).
- **`Libsystem`** — **NOW PINNED** (2026-09-04, `Libsystem-1292.60.1`,
  exact `macos-112` manifest match, verified via `git describe`/
  `git rev-list` against the manifest's real submodule commit
  `6e8c085dc78e04189936840e9f666e889680099d`). This entry's own prior
  text ("irrelevant, no dylib linking") was correct when written but is
  now stale — real dyld/per-library `.dylib` linking has existed since
  Phase 6.5 (2026-08-25), and this project's own duplicate-symbol dedup
  work (the `DAR-*` per-dylib series, `tools/userland_staging/
  dup_symbol_audit.sh`) needs a real, verifiable source for "which
  sub-dylib owns which symbol" ownership questions rather than asserting
  from general knowledge. Real finding from reading the pinned source:
  Libsystem's own `requiredlibs` file confirms `system_m`/
  `system_platform`/`compiler_rt` (among others) as separate,
  independently-built sub-projects combined into one umbrella
  `libSystem.B.dylib` — but Libsystem's own build (`xcodescripts/
  linker_arguments.sh`) does NOT itself enforce a duplicate-export check;
  it just gathers already-built dylibs. The real per-symbol ownership
  authority is each sub-project's own source tree (e.g. `libplatform`,
  already pinned as `libplatform-254.80.2`, for `memcpy`/`memset`), not
  Libsystem itself — Libsystem is useful for confirming the umbrella's
  real member list, not individual symbol assignments.
- **`vim`** — **NOW PINNED** (2026-09-18, `vim-91.80.1`, exact `macos-112`
  manifest match — `distribution-macOS`'s `macos-112` tree pins commit
  `effa1353bb4d0a6c70e008be5596a1c60c322593` for the `vim` submodule,
  which is exactly `vim-91.80.1`'s tag commit, confirmed via a real
  `git clone --branch vim-91.80.1`, not assumed from tag-name proximity).
  DAR-185 (Phase 7d). Apple's checkout ships a pre-generated
  `src/auto/config.h` (not run through `./configure` at build time) that
  is the real authority for which feature tier/optional subsystems this
  project's port should match: `FEAT_NORMAL` (not `SMALL`/`BIG`/`HUGE`),
  `TERMINFO`, `HAVE_ICONV`, `FEAT_JOB_CHANNEL`, `FEAT_NETBEANS_INTG`, and
  `FEAT_TERMINAL` (the last needing `src/libvterm`) all on; every
  scripting-language interpreter binding (`FEAT_PERL`/`PYTHON`/`RUBY`/
  `LUA`/`TCL`/`MZSCHEME`) and the GUI (`FEAT_GUI_GNOME` etc) off — i.e.
  real Apple's own shipped `/usr/bin/vim` is already a plain-terminal,
  non-scripted build, matching this project's own stated goal (no
  scripting-language runtime, no GUI) with no scope-narrowing needed on
  this project's part.

## Recommended next action

Part 1's two unambiguous mismatches (`IOStorageFamily`/`msdosfs`) are
fixed and reverified as of 2026-08-24 (see above). Remaining before
starting Part 2's new-library work:

- **`hfs`'s pin still needs a deliberate keep-or-revert decision**
  (currently `556.100.11`, newer than the manifest's `556.60.1`) — not
  mechanical like the other two, needs a call on whether manifest
  fidelity or the newer maintenance tag matters more here.
- ~~**`IOUSBFamily` has no better tag to move to** — confirmed no Big
  Sur-era manifest entry exists at all, nothing to fix.~~ **Wrong, fixed
  2026-09-08**: there was a better tag, just not a *newer* one.
  `630.4.5` ships its ten core `.cpp`s and eight core headers as
  zero-byte files upstream; re-pinned to `IOUSBFamily-560.4.2`, which is
  complete. See the 2026-09-08 addendum above.
- **Not yet done**: regenerating the legacy single-partition
  msdosfs-as-root test image (or an equivalent narrower test) to
  directly re-exercise msdosfs's own root-mount fixes at runtime, since
  the current default boot image never mounts `disk0s1` via msdosfs at
  all (it's HFS+-rooted; msdosfs's device node is enumerated but never
  mounted). Low urgency given msdosfs is already documented as the
  superseded root-fs path, but worth remembering it's an unverified gap
  before treating this fix as 100% closed rather than "compiles clean +
  boots clean with zero regression, runtime root-mount path unexercised."

Part 2's new-library list (`ncurses`, `gnutar`, `diskdev_cmds`, `awk`,
etc.) can proceed independently of the above.
