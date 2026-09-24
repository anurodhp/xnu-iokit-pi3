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
| `dyld` | `anurodhp/dyld` | `iokit-port` | apple-oss-distributions |
| `Libc` | `anurodhp/Libc` | `iokit-port` | apple-oss-distributions |
| `libdispatch` | `anurodhp/libdispatch` | `iokit-port` | apple-oss-distributions |
| `Libinfo` | `anurodhp/Libinfo` | `iokit-port` | apple-oss-distributions |
| `Csu` | `anurodhp/Csu` | `bcm2837-bringup` | apple-oss-distributions |
| `bash` | `anurodhp/bash` | `iokit-port` | apple-oss-distributions |
| `cron` | `anurodhp/cron` | `iokit-port` | apple-oss-distributions |
| `BSM` | `anurodhp/bsm` | `iokit-port` | apple-oss-distributions |
| `ICU` | `anurodhp/ICU` | `iokit-port` | apple-oss-distributions |
| `configd` | `anurodhp/configd` | `iokit-port` | apple-oss-distributions |
| `chrony` | `anurodhp/chrony` | `iokit-port` | mlichvar/chrony |
| `PureDarwin` | `anurodhp/PureDarwin` | `iokit-launchd-port` | PureDarwin/PureDarwin |
| `swift-corelibs-foundation` | `anurodhp/swift-corelibs-foundation` | `corefoundation-port` | apple/swift-corelibs-foundation |

On-disk remote naming varies. Repos converted from patch files keep
Apple as `origin` and the fork as `fork`, so push with `git push fork
<branch>`. The rest have the fork as `origin`. Check `git remote -v`
before pushing. A fresh `setup_third_party.sh` clone always has the fork
as `origin` and the upstream as `upstream`.

u-boot and dtc-AppleDeviceTree were the last two repos with leftover
patch files. On 2026-09-24 both forks' `HEAD^{tree}` was confirmed
identical to "upstream base + `git am` of the patch", and the patch
files were deleted.

---

## 2. Changed, but NOT on a fork yet (open tickets)

| Checkout | What's local | Ticket |
|---|---|---|
| `libpthread` | commit `58a80c8` (DAR-257 TSD fix) on local branch `bcm2837-bringup`, **on no remote**; the script clones plain upstream, so a fresh clone loses it | DAR-354 (High) |
| `IOHIDFamily` | patched in place by `tools/patch_iohid_gaps.sh` (DAR-245) | DAR-356 |
| `IOUSBFamily` | fixes applied to staged copies by `build_iousbfamily.sh` / `patch_iohid_gaps.sh` fix 6/6b; the checkout itself is clean | DAR-356 |
| `IOGraphics` | patched in place by `tools/patch_iographics_gaps.sh` (DAR-331) | DAR-355 |

---

## 3. Unmodified pins: no fork needed

Every other checkout is pinned to an exact upstream tag and carries no
local changes, so there is nothing of ours to lose. It can be re-cloned
from upstream at any time. Examples: `hfs`, `IOStorageFamily`,
`IONetworkingFamily`, `IOPCIFamily`, `IOSerialFamily`,
`AppleUSBCDCDriver`, `libmalloc`, `libplatform`, `Libm`, `Libnotify`,
the `*_cmds` repos, `ncurses`, `OpenSSH`, `vim`, `lzfse`, `xxHash`, and
the sparse `llvm-project`/`freebsd-src`/`sudo` clones.

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
