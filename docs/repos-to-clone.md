# Repos this project clones, and which ones need a fork you own

Re-audited 2026-09-24 (the first audit was 2026-08-30). Produced by
checking every checkout under `third_party/`: its `origin` and other
remotes, whether HEAD is on a remote branch, and whether the tree is
dirty. Compared against `setup_third_party.sh`'s real clone URLs. Not
written from memory.

`third_party/` is **gitignored**. Nothing under it is stored in this
repo, so any change made to a third-party repo is safe only if it is
committed **and pushed** to one of our forks.

**Standing rule (2026-09-24): every third-party repo we change is one of
our `anurodhp/*` forks.** No `third_party-patches/*.patch` files and no
`git am` step. The directory and the mechanism were retired on
2026-09-24. `setup_third_party.sh` now fails if a REPOS entry names a
patch file. Local-only commits and in-place patch scripts count as
violations too. When you find one, file a Linear ticket with the
`gitrepo` label.

The main repo itself is `anurodhp/xnu-iokit-pi3`.

---

## 1. Our forks: cloned directly, pushed

| Checkout | Fork | Branch | Upstream remote |
|---|---|---|---|
| `xnu-7195` | `anurodhp/xnu-pi3-macos11` | `bcm2837-bringup` | — |
| `xnu-latest` | `anurodhp/xnu-pi3-latest` | `main` | — |
| `qemu-zhuowei` | `anurodhp/qemu-xnu-arm` | `master` | — |
| `u-boot` | `anurodhp/u-boot` | `master` | FlorentRevest/u-boot |
| `dtc-AppleDeviceTree` | `anurodhp/dtc-appledevicetree` | `master` | darwin-on-arm/dtc-AppleDeviceTree |
| `dyld` | `anurodhp/dyld` | `xdm-greeter-fix` | apple-oss-distributions |
| `Libc` | `anurodhp/Libc` | `iokit-port` | apple-oss-distributions |
| `libdispatch` | `anurodhp/libdispatch` | `iokit-port` | apple-oss-distributions |
| `wmaker` | `anurodhp/wmaker` | `iokit-port` | window-maker/wmaker 0.96.0 + wrlib NEON fast path (DAR-437) + kqueue WUtil input wait (DAR-431); no upstream remote is added, this fork is the only source |
| `Libinfo` | `anurodhp/Libinfo` | `iokit-port` | apple-oss-distributions |
| `libpthread` | `anurodhp/libpthread` | `iokit-port` | apple-oss-distributions |
| `IOGraphics` | `anurodhp/IOGraphics` | `iokit-port` | apple-oss-distributions |
| `IOHIDFamily` | `anurodhp/IOHIDFamily` | `iokit-port` | apple-oss-distributions |
| `IOUSBFamily` | `anurodhp/IOUSBFamily` | `iokit-port` | apple-oss-distributions (off `IOUSBFamily-560.4.2`, DAR-421) |
| `AppleUSBCDCDriver` | `anurodhp/AppleUSBCDCDriver` | `iokit-port` | apple-oss-distributions (off `AppleUSBCDCDriver-4303.2.1`, DAR-448) |
| `Csu` | `anurodhp/Csu` | `bcm2837-bringup` | apple-oss-distributions |
| `bash` | `anurodhp/bash` | `iokit-port` | apple-oss-distributions |
| `cron` | `anurodhp/cron` | `iokit-port` | apple-oss-distributions |
| `BSM` | `anurodhp/bsm` | `iokit-port` | apple-oss-distributions |
| `ICU` | `anurodhp/ICU` | `iokit-port` | apple-oss-distributions |
| `configd` | `anurodhp/configd` | `iokit-port` | apple-oss-distributions |
| `chrony` | `anurodhp/chrony` | `iokit-port` | mlichvar/chrony |
| `PureDarwin` | `anurodhp/PureDarwin` | `iokit-launchd-port` | PureDarwin/PureDarwin |
| `swift-corelibs-foundation` | `anurodhp/swift-corelibs-foundation` | `corefoundation-port` | apple/swift-corelibs-foundation |
| `gnustep-base` | `anurodhp/libs-base` | `iokit-port` | gnustep/libs-base (off `base-1_31_1`; objc4 port DAR-388, NSProxy weak methods + BOOL decode DAR-450, gdnc stdio / getpw*_r / NSNumber BOOL DAR-451) |
| `gnustep-make` | `anurodhp/tools-make` | `iokit-port` | gnustep/tools-make (off `make-2_9_3`; extract objc4 class names on Darwin, DAR-451) |
| `gnustep-gui` | `anurodhp/libs-gui` | `iokit-port` | gnustep/libs-gui (off `gui-0_32_0`; objc4 port, DAR-390) |
| `gnustep-examples` | `anurodhp/tests-examples` | `iokit-port` | gnustep/tests-examples (master 46ef973, unmodified; pins the commit) |
| `gnustep-gap` | `anurodhp/gap` | `iokit-port` | gnustep/gap (master 220860a9; Terminal Darwin build, DAR-394) |
| `rome` | `anurodhp/rome` | `v0.1` (tag) | none: this project's own repo (Rome.app terminal, build_rome.sh) |
| `gnustep-gworkspace` | `anurodhp/apps-gworkspace` | `iokit-port` | gnustep/apps-gworkspace tag gworkspace-1_0_0 (14ea9f49); typed IMPs for objc4 DAR-392, longer fswatcher/ddbd start wait DAR-451 |
| `gnustep-eau-theme` | `anurodhp/gershwin-eau-theme` | `iokit-port` | gershwin-desktop/gershwin-eau-theme main 3d741fbc (LGPL-2.1); no NSRegularExpression, GWDialog via class_addMethod, EAU_NO_TEST (DAR-461) |
| `gershwin-windowmanager` | `anurodhp/gershwin-windowmanager` | `iokit-port` | gershwin-desktop/gershwin-windowmanager main 10b6797 (MIT); compositing off by default, IOKIT_PORT=1 build (Gershwin M2) |

On-disk remote naming varies. Repos converted from patch files keep
Apple as `origin` and the fork as `fork`, so push with `git push fork
<branch>`. The rest have the fork as `origin`. Check `git remote -v`
before pushing. A fresh `setup_third_party.sh` clone always has the fork
as `origin` and the upstream as `upstream`.

`libpthread`, `IOGraphics` and `IOHIDFamily` were moved to forks on
2026-09-27 (DAR-354/355/356). Each fork branch was checked to match the
live checkout's content exactly (`diff -rq`). Checkouts made before that
date still have Apple as their only remote: libpthread is on a local
`bcm2837-bringup` branch, and IOGraphics/IOHIDFamily sit at a detached
upstream tag with a dirty tree. To switch one over in place without
changing any file:
`git remote rename origin upstream && git remote add origin
git@github.com:anurodhp/<repo>.git && git fetch origin && git checkout
-f -B iokit-port origin/iokit-port && git clean -fd`. The `-f` and the
`git clean` are needed for IOGraphics and IOHIDFamily. Their dirty and
untracked files already equal the fork's content, but a plain checkout
refuses to overwrite them and leaves the repo half-switched: remotes
renamed, still on the old tag. Re-cloning (`rm -rf third_party/<repo>`,
then `setup_third_party.sh`) also works.

Status 2026-09-28: the main checkout's libpthread, IOGraphics and
IOHIDFamily are all switched now (branch `iokit-port` tracking remote
`fork`; Apple stays `origin`). gnustep-base was forked the same day
(remote `fork`, upstream gnustep as `origin`).

`IOUSBFamily` was forked on 2026-09-28 (DAR-421). `iokit-port` is
`IOUSBFamily-560.4.2` plus seven commits, one per fix that used to be
applied to staged or vendored copies (`build_iousbfamily.sh`,
`vendor_usb_into_xnu.sh` FIXES 1/1b/1c/1d/4, `patch_iohid_gaps.sh` fixes
6/6b; the last script is retired). The code is the same as the copies
xnu-7195 already vendored; only the in-source comments differ (they cite
the fork, not those scripts), so xnu got a comment-only re-vendor. The
main checkout was switched in place (Apple stays `origin`, the fork is
remote `fork`). An older checkout still at the detached
`IOUSBFamily-560.4.2` tag has a clean tree, so a plain
`git remote add fork git@github.com:anurodhp/IOUSBFamily.git && git fetch fork && git checkout -B iokit-port fork/iokit-port`
switches it, or re-clone.

`AppleUSBCDCDriver` was forked on 2026-09-29 (DAR-448). `iokit-port` is
`AppleUSBCDCDriver-4303.2.1` plus two commits, one per fix that
`vendor_usb_into_xnu.sh` (FIXES 2/3) used to apply at vendor time:
`linkup.h`'s `const char *period` and `AppleUSBCDCECMData.cpp`'s
runt-frame check (DAR-374). An older checkout at the detached tag has a
clean tree, so `git remote add fork git@github.com:anurodhp/AppleUSBCDCDriver.git && git fetch fork && git checkout -B iokit-port fork/iokit-port`
switches it, or re-clone.

u-boot and dtc-AppleDeviceTree were the last two repos with leftover
patch files. On 2026-09-24 both forks' `HEAD^{tree}` was confirmed
identical to "upstream base + `git am` of the patch", and the patch
files were deleted.

---

## 2. Changed, but NOT on a fork yet (open tickets)

None open as of 2026-09-29 (DAR-448 was the last). When the audit finds
one, add a row here with its ticket.

---

## 3. Unmodified pins: no fork needed

Every other checkout is pinned to an exact upstream tag and carries no
local changes, so there is nothing of ours to lose. It can be re-cloned
from upstream at any time. Examples: `hfs`, `IOStorageFamily`,
`IONetworkingFamily`, `IOPCIFamily`, `IOSerialFamily`,
`libmalloc`, `libplatform`, `Libm`, `Libnotify`,
the `*_cmds` repos, `ncurses`, `OpenSSH`, `vim`, `lzfse`, `xxHash`, and
the sparse `llvm-project`/`freebsd-src`/`sudo` clones.
The 17 X.org client-library pins added for DAR-383 (`xorgproto`, `xtrans`,
`xcb-proto`, `libXau` … `libXft`, freedesktop.org release tags) are in this
group too, as are `libXpm`, `libXaw` and `xdm` (xdm-login branch; release
tags, no local changes, so no fork is needed).

A plain `git log --not --remotes` over these reports large "unpushed"
counts. That's an artifact of shallow/tag clones (release-tag history
that isn't on a fetched remote-tracking branch), not project work.
Check commit subjects instead.

---

## Re-running this audit

```
cd third_party
for d in */; do d=${d%/}; [ -e "$d/.git" ] || continue
  echo "$d $(git -C $d remote -v | awk '/fetch/{print $1"="$2}' | tr '\n' ' ')" \
       "dirty=$(git -C $d status --porcelain | wc -l)" \
       "on-remote=$(git -C $d branch -r --contains HEAD | head -1)"
done
```

A repo needs attention if it is dirty, or if it has project commits and
HEAD is on no remote branch of a fork. To prove a fork matches some other
record (an old patch file, say), use a tree-hash replay, not commit
counts: apply the record to the upstream base and compare
`git rev-parse 'HEAD^{tree}'`.
