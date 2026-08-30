# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

A from-source port of XNU (Darwin 20.3.0 / xnu-7195) to the Raspberry Pi 3
(BCM2837), booted primarily under QEMU (`-M raspi3`) with real hardware as
a secondary target. There is no launchd, no opendirectoryd, no
prelink/kext-cache pipeline — userland's PID-1 (`tools/init_binary/`) is a
custom static binary and the handful of IOKit drivers under `iokit/` are
compiled directly into the kernel, not loaded as kext bundles. **As of
2026-08-25, real dyld-940 exists and boots** (Phase 6.5, see
`docs/daemon-roadmap.md`) — it loads a real, dynamically-linked test
executable against real `/usr/lib/system/libdyld.dylib` and
`/usr/lib/libSystem.B.dylib` and reaches the program's own real `main()`.
**New project convention going forward: prefer real, per-library
`.dylib`s over new static archives.** The existing curated libc/
libpthread/libmalloc/libplatform/etc. stack (`tools/userland_staging/
build_*.sh`) still ships as static archives from before dyld existed, and
`libSystem.B.dylib` is currently just that same static stack flattened
into one dylib rather than real, separate per-library dylibs matching
Apple's actual modular architecture — a known, flagged migration debt
(see `docs/daemon-roadmap.md`'s "Static-vs-dylib architecture debt"
section), not a design goal to replicate for new work.
`tools/init_binary/build.py`/`tools/pwtool/build.py` (PID-1 and the
password tool specifically) are the one legitimate exception that should
stay fully static, since they run before/without dyld by design. See
`docs/roadmap.md` for the full multi-phase plan and current phase status,
and `docs/backlog.md` for the open bug/risk list (P0 = real-hardware risk,
P1 = correctness bugs, P2 = known gaps, P3 = hygiene).

## Setup

```
./setup_third_party.sh           # clone all third_party/ deps (gitignored, own git histories)
./setup_third_party.sh --force   # wipe and re-clone everything
```

`third_party/` holds: this project's own forks (`xnu-7195`, `xnu-latest`,
`qemu-zhuowei`) with full port history; small patched upstreams
(`dtc-AppleDeviceTree`, `u-boot`, patches in `third_party-patches/`); and
40+ unmodified Apple open-source repos pinned to exact tags matched
against apple-oss-distributions' `distribution-macOS` `macos-112`
manifest for macOS 11.2/Darwin 20.3 (the libSystem stack, `bash`/
coreutils, `hfs`/`msdosfs`/`IOStorageFamily`/`IOUSBFamily`/
`IONetworkingFamily`, and more). **`docs/apple-oss-catalog.md` is the
authoritative, up-to-date list** — every pin's tag, whether it matches
the manifest, and what's cataloged-but-not-yet-pinned; don't trust an
inline list here to stay current. That doc also documents a real lesson:
a full pin audit (2026-08-24) found 2 of 32 pins had drifted to a tag
*older* than what the manifest actually specifies (`IOStorageFamily`,
`msdosfs`) despite this file's own "pinned to an exact tag" claim — a
pin is only as trustworthy as the last time someone actually diffed it
against the manifest, not merely present in this file.

## Build commands

```
tools/build_xnu_7195.sh          # build the kernel (third_party/xnu-7195, branch bcm2837-bringup)
tools/build_kexts.sh [outdir]    # compile iokit/*/*.cpp standalone (default: build/kexts)
```

- `build_xnu_7195.sh` requires the live/daily-driver Xcode's `iPhoneOS`
  SDK (`SDKROOT=iphoneos`, `PLATFORM=iPhoneOS` — BCM2837 is embedded-only
  upstream; do not mix with `SDKROOT=macosx`, it desyncs `CONFIG_EMBEDDED`
  vs `XNU_TARGET_OS_OSX` struct layouts). The script publishes its own
  output to
  `build/xnu-latest-build/out/kernel.development.bcm2837-7195.unstripped`
  automatically (via `tools/sync_iokit_drivers.sh` + a `cp` at the end) —
  this used to be a manual step and isn't anymore; don't manually copy on
  top of it.
- `build_kexts.sh` requires **Xcode 12** specifically
  (`DEVELOPER_DIR=/Applications/Xcode-12.app/...`), not the daily-driver
  Xcode — its SDK vintage lines up with xnu-7195's Darwin 20.3, and
  `xcodebuild`/`xcrun` are broken on current macOS anyway (crash loading a
  removed private framework), so it invokes `clang++` directly using
  `compile_flags.txt`. Kexts built this way: `BCM2837InterruptController`,
  `BCM2837SDHCI`, `SMSC9514Ethernet`.
- `tools/init_binary/build.py`, `tools/pwtool/build.py` — build the
  freestanding userland Mach-O binaries (see below). Both warn loudly if
  `otool -l` output isn't fully static (`LC_UNIXTHREAD`, zero
  `LC_LOAD_DYLIB`) — that invariant is load-bearing, not cosmetic.
- `tools/userland_staging/build_*.sh` — 30+ scripts, one per curated
  libSystem/libc library or real command, each producing a static
  archive/binary under `tools/userland_staging/libc_build/`. This is a
  DIFFERENT toolchain setup than either build command above: **Xcode 12's
  `iPhoneOS` SDK** (not the daily-driver Xcode, not the kernel's SDK path)
  at `/Applications/Xcode-12.app/Contents/Developer/Platforms/
  iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk`, target
  `arm64-apple-ios14.4`, invoking `clang`/`clang++` directly (never
  `xcodebuild`). Every archive is compiled `-ffreestanding -fno-builtin
  -fno-stack-protector`. Most of this stack predates real dyld and still
  ships as static archives (`otool -l` shows zero `LC_LOAD_DYLIB` and a
  real `LC_UNIXTHREAD`, checked by every script's own smoketest step) —
  but real dyld now exists (Phase 6.5), and `build_libdyld.sh`/
  `build_libsystem.sh`/`build_dylib_smoketest.sh` DO produce real
  `.dylib`s on purpose. New work should prefer real per-library dylibs
  over new static archives going forward (see `docs/daemon-roadmap.md`'s
  "Static-vs-dylib architecture debt" section) — the "must stay fully
  static" invariant now applies specifically to `tools/init_binary/
  build.py`/`tools/pwtool/build.py` (real reasons: they run before/
  without dyld by design), not to every script in this directory.
  **`build_libc_regex.sh` is the reference template for porting one more
  Apple library** (real per-file compile loop reporting gaps, real `ar`
  archive step, real dependency check against already-built `.a` files,
  real smoketest linking the new archive against `third_party/Csu`'s
  `start.s`/`crt.c` plus the existing curated stack, checked via `nm -u`
  for undefined symbols) — read it before writing a new one rather than
  inventing a different shape. `build_libc_locale.sh` has the most fully
  commented version of the shared `CFLAGS` recipe itself. Real dependency
  order matters (e.g. `libc_regex.a` needs `libc_locale.a` and
  `libmalloc.a` already built) — each script checks its own prerequisites
  and fails fast rather than guessing.
  New library/tool ports follow this exact loop: pin the tag in
  `setup_third_party.sh` against the `macos-112` manifest (see
  `docs/apple-oss-catalog.md`), clone it, write `build_<name>.sh` off the
  template above, run it for real, verify with a real smoketest — no
  fabricated stubs beyond a clearly-documented, provable architectural
  gap (e.g. OpenPAM's `dlopen`-based module loader has no home here since
  there's no dyld at all; that's a real gap to document, not paper over).
  This kind of independent library port is a good candidate for
  delegating to parallel background subagents (one per library) to keep
  context small — but have the orchestrating session make any *shared*
  file edits (`setup_third_party.sh`'s pins, in particular) itself first,
  so parallel agents never touch the same file.

## Boot / test commands

```
./runvm.sh                                   # boots the from-source kernel + real 2-partition HFS+ SD image
SD_IMAGE=qemu/fat32_test.dmg ROOT_DEV=disk0 ./runvm.sh   # older single-partition msdosfs image
```

`runvm.sh` uses `third_party/qemu-zhuowei` (zhuowei's QEMU fork) —
**never** substitute plain `qemu-system-aarch64` or `tools/direct_boot`;
both lack qemu-zhuowei's secondary-core-parking patch and won't boot
correctly. Key boot-args baked into the script: `serial=3` (required for
serial keyboard *input* to reach the shell, not just output),
`cpu-factor=10` (scales XNU's `INTERRUPT_MASKED_DEBUG` watchdog timeout —
QEMU/TCG's wall-clock jitter under host scheduling contention otherwise
produces a real, ~40%-reproducible false-positive panic; this is the same
mechanism real FPGA prototype hardware uses via `target-is-fpga`, and is
deliberately QEMU-only — real hardware's u-boot boot chain is untouched).

Interact with a running QEMU instance via its monitor socket
(`qemu/mon.sock`). Use monitor `x` (virtual memory) for kernel/userspace
addresses, **not** `xp` (physical) — using `xp` on a guest virtual address
silently reads zeroed/garbage RAM and has produced false diagnoses before.

Host-side logic testing without booting a kernel:
```
python3 tools/init_binary/host_test.c   # (compiled) — INIT_HOST_TEST guards the syscall layer
python3 tools/init_binary/_pty_test_phase5.py   # real pty-driven interactive shell test
```

## Architecture

See `architecture.md` for the full write-up (boot chain, driver stack,
userland). Read it before making non-trivial changes — this project has a
long history of "obvious" fixes turning out to be one layer above or below
the real bug (see `docs/backlog.md` and the roadmap's phase write-ups for
worked examples). Other `docs/` files worth knowing about beyond
`roadmap.md`/`backlog.md`: `daemon-roadmap.md` (background-daemon
porting plan/tiering), `apple-oss-catalog.md` (full apple-oss-
distributions inventory + pin-correctness audit),
`launchd-bug6-postmortem.md` + `launchd-pid1-postmortem.md` (the two
real launchd PID-1 investigations, in that order — read both before
touching the PID-1 boot path; between them they cover five separate
duplicate-static-copy bugs, `fork()`/atfork, and a silent
uninitialized-`__atexit` hang), `static-linking-cleanup-plan.md`,
`multiuser-userland-plan.md`, `hfs-integration-notes.md`;
`docs/historic/` archives full phase-by-phase detail once
`roadmap.md`/`backlog.md` are condensed, plus one-shot planning docs
whose work is now done and real-hardware-verified (`usb-support-plan.md`,
`sd-and-ethernet-plan.md` — USB/SD/Ethernet are all real-hardware-
verified, see `docs/roadmap.md`'s Phase 3/6a/6b).

`third_party/xnu-7195` and `third_party/qemu-zhuowei` are this project's
own forks with their own real git history and branches — not just a
pinned tag. `bcm2837-bringup` is the kernel fork's main line
(`build_xnu_7195.sh` builds it by name). Larger efforts sometimes happen
on a side branch first (e.g. `phase-6b-inet` for the Phase 6b networking
work) and get fast-forward-merged back into `bcm2837-bringup` once
real-hardware/QEMU-verified — check `git branch -vv` in that submodule
before assuming `bcm2837-bringup` is checked out or up to date; it has
drifted before.

## Working conventions specific to this repo

- **Source priority**: try Apple's real open-source version of a component
  first (apple-oss-distributions, pinned via `setup_third_party.sh`); if
  Apple never open-sourced it as a standalone repo at all (confirmed via a
  real `git ls-remote`, not assumed), fall back to another real upstream
  open-source project Apple's own implementation is downstream of or
  equivalent to (FreeBSD for BSD userland/libc gaps; LLVM upstream for
  libc++/libc++abi -- the actual codebase Apple's shipped libc++ is
  downstream of, just not packaged as its own apple-oss-distributions repo
  for every OS era, e.g. `llvmorg-11.1.0`'s `libcxx`/`libcxxabi`, pinned
  2026-08-25 for the dyld port); write custom code only as a last resort
  (e.g. `libc_locale` string/locale stubs, the from-scratch SHA-256/384/512
  replacing the corecrypto-kext-dependent implementation).
- **Resolve known dependencies before booting QEMU**: before a real QEMU
  boot test on a new/changed userland binary that links libdispatch/
  libsystem_*/etc, run `tools/userland_staging/gap_audit.sh` against it
  (target dylib/macho + the other images it loads alongside) and close
  every real gap the script marks `FREEWIN` (already a real, defined
  export of some already-built archive, just not linked in yet) first.
  A QEMU boot-and-crash cycle finds real gaps one dyld-abort at a time
  (real, but slow); `gap_audit.sh` finds the same real gaps from `nm -m`
  in seconds, plus classifies each one against real Apple source (BSD
  syscall/Mach trap/MIG routine/needs manual investigation) so the next
  real port step is obvious. Written 2026-08-27 after closing 20 of a
  dylib's 30 real remaining-gap symbols in one pass and needing this
  exact triage by hand each time — don't re-derive it manually again.
- **Adversarial review before commit**: run an adversarial review on
  large/significant changes *before* committing, not after — post-hoc
  reviews in this project's history have caught real bugs (a false-positive
  instrumentation counter, a real crash race) already committed/pushed.
- **Verify claims against source, not by date/name proximity** — e.g. the
  exact apple-oss-distributions tag pins here were confirmed via
  `distribution-macOS`'s `macos-112` manifest matching xnu-7195's real
  submodule commit, not guessed.
- Commit ≠ push: don't push unless asked to. Gitignore large/reproducible
  binaries and build intermediates (see `.gitignore`'s inline comments,
  which explain *why* each entry is excluded — read them before adding new
  build output to the ignore list).
- Kernel driver personalities are hand-built in
  `iokit/BCM2837ArmIO/BCM2837Personalities.cpp` (no Info.plist/prelink
  pipeline exists here) — if a new driver isn't matching, check this file
  before assuming the driver code itself is broken.
- **Every built artifact needs a reproducing script, checked in** — no
  manual/ad-hoc build steps that only exist in a shell history or an
  agent's transcript. Any new binary, kext, image, or userland command
  (e.g. new entries in `tools/userland_staging/build_simple_cmds.sh`, new
  `tools/userland_staging/build_*.sh` scripts, new
  `tools/init_binary/inject_into_sd_image.sh` copy-loop entries) must be
  producible end-to-end by a script in the repo, so cloning fresh
  (`git clone` + `setup_third_party.sh` + the documented build commands
  above) reproduces it with no undocumented manual steps. If a fix needs a
  one-off manual command to verify, promote it into the relevant build
  script before considering the work done.
