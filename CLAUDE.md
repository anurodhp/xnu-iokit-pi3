# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

A from-source port of XNU (Darwin 20.3.0 / xnu-7195) to the Raspberry Pi 3
(BCM2837), booted primarily under QEMU (`-M raspi3`) with real hardware as
a secondary target. There is no dyld, no launchd, no opendirectoryd, no
prelink/kext-cache pipeline — userland is a custom static PID-1
(`tools/init_binary/`) and the handful of IOKit drivers under `iokit/` are
compiled directly into the kernel, not loaded as kext bundles. See
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
unmodified Apple open-source pinned to exact tags matched against
apple-oss-distributions' `distribution-macOS` manifest for macOS 11.2
(`hfs`, `msdosfs`, `IOStorageFamily`, `IOUSBFamily`, `IONetworkingFamily`,
the libSystem stack, `bash`/coreutils).

## Build commands

```
tools/build_xnu_7195.sh          # build the kernel (third_party/xnu-7195, branch bcm2837-bringup)
tools/build_kexts.sh [outdir]    # compile iokit/*/*.cpp standalone (default: build/kexts)
```

- `build_xnu_7195.sh` requires the live/daily-driver Xcode's `iPhoneOS`
  SDK (`SDKROOT=iphoneos`, `PLATFORM=iPhoneOS` — BCM2837 is embedded-only
  upstream; do not mix with `SDKROOT=macosx`, it desyncs `CONFIG_EMBEDDED`
  vs `XNU_TARGET_OS_OSX` struct layouts). After a rebuild, manually copy
  the output to
  `build/xnu-latest-build/out/kernel.development.bcm2837-7195.unstripped`
  — no script does this copy automatically.
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
- `tools/userland_staging/build_*.sh` — incremental libSystem/libc pulls
  (Phase 4): `build_libc_string.sh`, `build_libc_locale.sh`,
  `build_crt0_smoketest.sh`, `build_libsyscall_smoketest.sh`,
  `build_tsd_bootstrap.sh`, `build_pthread_tsd_shim_smoketest.sh`.

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
worked examples).

## Working conventions specific to this repo

- **Source priority**: try Apple's real open-source version of a component
  first (apple-oss-distributions, pinned via `setup_third_party.sh`); fall
  back to FreeBSD only if Apple's is too complex/closed for the current
  need; write custom code only as a last resort (e.g. `libc_locale`
  string/locale stubs, the from-scratch SHA-256/384/512 replacing the
  corecrypto-kext-dependent implementation).
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
