# Architecture

XNU-on-rpi3: a from-source port of XNU (xnu-7195, Darwin 20.3.0 / macOS
11.2) to the Raspberry Pi 3 (BCM2837), booted under QEMU's `raspi3`
machine model (primary target) or real hardware (secondary, blocked on a
serial cable for confirmation — see `docs/roadmap.md`). This document
describes the pieces and how they fit together; `docs/roadmap.md` and
`docs/backlog.md` describe status and open issues.

## Big picture: boot chain

```
u-boot (real HW only)  or  qemu-zhuowei's raspi3 loader (QEMU)
        |
        v
XNU kernel entry (arm64, third_party/xnu-7195, board config BCM2837)
        |
        v
pexpert / IOKit bring-up (IODTPlatformExpert subclass, device-tree nubs)
        |
        v
Driver matching cascade (iokit/*, hand-built personalities — no kext bundles)
        |
        v
vfs_mountroot() -> real 2-partition SD card (FAT32 boot + HFS+ root)
        |
        v
bsd_init() -> load_init_program() -> execs real, dynamically-linked
                                       launchd.macho as PID 1
```

QEMU boots via `third_party/qemu-zhuowei` specifically (not stock
`qemu-system-aarch64`) because it carries a local patch to park secondary
cores the way real firmware does. Real hardware follows the
same kernel/DT artifacts through u-boot's `bootxnu`, but boot verification
there is currently unconfirmed past kernel entry (no serial cable).

## Kernel

`third_party/xnu-7195` (branch `bcm2837-bringup`, this project's own fork)
is real, unmodified-except-for-port-fixes apple-oss-distributions source —
not a binary patch of a precompiled kernel. `MACHINE_CONFIGS=BCM2837`,
`KERNEL_CONFIGS=DEVELOPMENT`. Built as an embedded (`iPhoneOS` SDK)
target; do not build against `macosx` SDK, see CLAUDE.md.

Fixes landed in the kernel tree itself (not this repo's `iokit/`) include:
locale/TSD bootstrap plumbing for userland (`cswitch.s` TPIDRRO_EL0
wiring), duplicate sysctl registration fixes, `IOSecureBSDRoot()` timeout
tolerance, `INTERRUPT_MASKED_DEBUG` watchdog tolerance via the real
`cpu-factor`/`debug_cpu_performance_degradation_factor` pexpert mechanism,
and — most structurally significant — closing a real ARM64 IRQ delivery
gap: `sleh_irq()` (the actual exception vector target) dispatches through
a per-CPU `interrupt_handler` function pointer that is only ever installed
by `IOCPUInterruptController::enableCPUInterrupt()`, which is only
reachable if a concrete `IOCPU` subclass exists and registers — this
project had none until `iokit/BCM2837CPU` was added, meaning **no real
ARM64 hardware interrupt had ever reached XNU's dispatch code before
that**; all interrupt-driven I/O before it was polling-only.

## This repo's IOKit drivers (`iokit/`)

All compiled directly into the kernel via `iokit/conf/files` — there is no
kext bundle / Info.plist / prelink-cache pipeline in this bare Mach-O boot
path, so driver matching depends entirely on the hand-built personality
table in `iokit/BCM2837ArmIO/BCM2837Personalities.cpp` (read that file's
header comment — it documents two real gaps that had to be closed: nub
personalities for device-tree-derived nodes, and the fact that
`IODTPlatformExpert::configure()`'s `processTopLevel()` never runs at all
unless a concrete platform-expert subclass first matches the *root* nub).

- **`BCM2837PlatformExpert`** — trivial concrete `IODTPlatformExpert`
  subclass; without it, root is only ever claimed by the built-in
  `IOPanicPlatform` catch-all (score 0, deliberately loses to any real
  match), and no device-tree nub creation happens for any node.
- **`BCM2837ArmIO`** — arm-io bus enumeration / personalities, described
  above.
- **`BCM2837CPU`** — concrete `IOCPU` for the single effectively-running
  core (secondary cores are parked below IOKit). Its `initCPU()` is what
  installs the real ARM64 interrupt handler via
  `IOCPUInterruptController::enableCPUInterrupt()` — see kernel section
  above. Also the (idempotent, safety-net) second caller of
  `IOService::cpusRunning()`.
- **`BCM2837LocalIntc`** — the BCM2836/7 L1 ("local interrupt controller")
  driver, IRQ 8's fan-in from the global controller; registers against
  `IOCPUInterruptController` as source 0.
- **`BCM2837InterruptController`** — the BCM2835-style "armctrl-ic" global
  peripheral interrupt controller (Linux: `brcm,bcm2836-armctrl-ic`),
  multiplexing GPIO/aux/SDHCI IRQs onto the L1 controller. Vector
  numbering convention: 0-31 = bank 1, 32-63 = bank 2, 64-71 = basic
  (`bank*32 + bit`). This mapping had a real, previously-undetected bug:
  generic IOKit's `IODTMapOneInterrupt()` stores a device's multi-cell
  `interrupts` DT property unprocessed, and the base
  `IOInterruptController::registerInterrupt()` only reads the *first*
  32-bit cell as the vector — so a `{bank, bit}` 2-cell node like SDHCI's
  `interrupts = <2 30>` registered on vector 2, not the intended 62,
  silently missing every real hardware interrupt. Fixed by switching the
  device tree to single-cell, pre-flattened vectors
  (`devicetrees/rpi3.dts`) rather than teaching the controller to decode
  multi-cell encodings generically.
- **`BCM2837SDHCI`** — the SD host controller driver; real end-to-end
  block reads verified in QEMU, real interrupt-driven I/O confirmed (zero
  poll-fallback hits) after the interrupt-delivery fix above. Boots off a
  real 2-partition SD image (MBR partition 1 = FAT32 boot firmware,
  partition 2 = HFS+ root), root selected via `IOFDiskPartitionScheme`
  (`disk0s2`).
- **`SMSC9514Ethernet`** — networking is out of current scope (this board
  has no working USB host controller driver — see USB note below — so no
  network interface is realistically reachable yet); INET/INET6 domain
  registration is currently disabled in `bsd_init()`'s callees as a
  deliberate scope decision, not a bug.

## Storage stack

Two-partition real SD card image: FAT32 boot firmware partition +
HFS+ root partition. `hfs` and `IOStorageFamily` are unmodified Apple
sources (pinned tags). Root mount path:
`vfs_mountroot()` → `IOFDiskPartitionScheme` resolves `disk0s2` →
`hfs_mountroot()` mounts HFS+ as root (first real non-mockfs root mount in
this project's history). `msdosfs` is also wired in and used for the
single-partition boot images (`ROOT_DEV=disk0`) and the FAT32 boot
partition itself. `devfs` auto-mounts at `/dev`.

`mockfs` (`bsd/miscfs/mockfs/`) was the original, now-superseded boot
mechanism: an Apple mechanism (not invented for this project) that treats
a device-tree `RAMDisk` memory-map region as a single Mach-O file and
execs it directly as `/sbin/launchd`, with no real directory tree (no
`VNOP_MKDIR`/`VNOP_CREATE`). Superseded by the real SD/HFS+ path above.
This project's original PID-1, `tools/init_binary/init.c` (a custom,
fully static, freestanding fork/exec/wait shell with no dyld dependency —
built to satisfy exactly this mockfs-era constraint), was itself retired
2026-08-31 once real launchd.macho became PID 1 (see below); see
`docs/static-linking-cleanup-plan.md` and Linear DAR-166/DAR-106/DAR-137
for the retirement record. `tools/init_binary/` today holds only
`inject_into_sd_image.sh`, the real SD-image installer for
`launchd.macho`/`pwtool.macho`.

## Userland

No opendirectoryd — never open-sourced by Apple and would require a huge
dependent stack (CoreFoundation, XPC) to replicate. Real dyld (Phase 6.5)
and real, ported `launchd.macho` (Phase 6.8, PID 1) both exist and boot
today — see `docs/daemon-roadmap.md` for the full porting history; this
file's userland section predates both and describes the earlier
static-archive/custom-PID-1 bring-up phase below for historical context.

- **`tools/userland_staging/`** now also builds real per-library
  `.dylib`s (`libdyld.dylib`, `libSystem.B.dylib`, `launchd.macho` and
  its dependency graph) on top of the static bring-up described next —
  see `CLAUDE.md`'s "New project convention going forward" note and
  `docs/daemon-roadmap.md`'s "Static-vs-dylib architecture debt" section.
- **`tools/userland_staging/`** — incremental libSystem/libc bring-up
  (Phase 4): real Csu `crt0`, real libsyscall stubs, a real `libc_string.a`
  (20 Apple + 9 FreeBSD-fallback functions), TSD bootstrap
  (`tsd_bootstrap.c` — sets `TPIDRRO_EL0` via the kernel's existing
  "set cthread self" platform syscall on every context switch, no kernel
  changes required) unblocking ~113 locale/wide-char Libc source files
  linked with zero undefined symbols, and a minimal 5-function stdio
  subset closing what was previously a hard wall.
- **`tools/pwtool/`** and **`tools/libinfo_custom/`** — a custom flat-file
  account resolver standing in for opendirectoryd (Libinfo can call a
  custom resolver directly, no directory-service daemon needed) plus a
  from-scratch DES crypt implementation (`crypt_des.c`) for password
  hashing, following the same "write custom only as last resort" priority
  as the locale stubs and the from-scratch SHA-256/384/512 (replacing the
  corecrypto-kext-dependent implementation that never had `g_crypto_funcs`
  registered, since there's no corecrypto kext here).
- **GNUstep** (`build_gnustep_*.sh`, X11 + Window Maker session) is built
  in gnustep-make's "gnustep" layout under `/usr/GNUstep`
  (`libc_build/gnustep/root`), but the image gets the Mac layout
  (gnustep-make `FilesystemLayouts/mac`): apps in `/Applications`
  (`/Applications/Utilities` for admin apps), the system Library
  (libraries, frameworks, bundles, themes, Makefiles) in `/System/Library`
  beside `SystemConfiguration`, docs in `/Library/Documentation`, tools in
  `/usr/local/bin`, and `/etc/GNUstep/GNUstep.conf` naming those paths
  (gnustep-base reads it at process start). The libraries keep their
  `/usr/GNUstep/System/Library/...` install names; compatibility symlinks
  at `/usr/GNUstep/System/{Applications,Library,Tools}` resolve them.
  `stage_gnustep_mac_layout.sh` does the rearrangement for
  `inject_into_sd_image.sh`. User paths stay `~/GNUstep/...`.
- **Graphical login: gsdm** (`build_gsdm.sh`, `third_party/gsdm` =
  `anurodhp/gsdm`, this project's own GNUstep display manager; it replaced
  xdm on 2026-10-06). launchd runs `/usr/local/sbin/gsdm`
  (`org.puredarwin.gsdm.plist`), which exits 0 unless the boot-args carry
  `hdmi_console=1` and `/etc/x11-session.disabled` is absent
  (`/etc/gsdm/gsdm.conf`). Otherwise it starts Xorg with an
  MIT-MAGIC-COOKIE-1 `-auth` file, shows
  `/System/Library/CoreServices/LoginWindow.app` (user tiles, password,
  Restart/Shut Down via `/sbin/reboot`/`/sbin/halt`), checks the password
  with `crypt(3)` against `/etc/master.passwd` (as `login` does), runs
  `/etc/gsdm/Xsession` (Window Maker) as the user, and restarts Xorg after
  logout -- xdm's own sequence, followed step by step in gsdm's
  `daemon/gsdm.c`. Log: `/var/log/gsdm.log`.


