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
bsd_init() -> load_init_program() -> execs tools/init_binary/init.macho as PID 1
        |
        v
Custom static PID-1: fork/exec/wait/reap, shell (ls/cat/cd/pwd/echo/mkdir/bash)
```

QEMU boots via `third_party/qemu-zhuowei` specifically (not stock
`qemu-system-aarch64`) because it carries a local patch to park secondary
cores the way real firmware does; `tools/direct_boot` is an older,
less-reliable path kept for reference only. Real hardware follows the
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
`VNOP_MKDIR`/`VNOP_CREATE`). Superseded by the real SD/HFS+ path above,
but `tools/init_binary/`'s design (fully static, no dyld, virtual in-
process command dispatch) still reflects constraints that mattered most
under mockfs and remain true today (no dyld regardless of root fs).

## Userland

No dyld, no launchd, no opendirectoryd — none of these are open-sourced by
Apple and would require a huge dependent stack (CoreFoundation, XPC) even
if they were.

- **`tools/init_binary/`** — the actual PID-1 binary the kernel execs
  (`init.macho`, statically linked, `LC_UNIXTHREAD` entry, zero
  `LC_LOAD_DYLIB`). Implements real `fork`/`exec`/`wait`/reap (found and
  fixed a real arm64 ABI quirk: the child-process flag comes back in `x1`,
  not `x0`, on this kernel's `fork()` return convention) and a shell with
  both virtual/in-process builtins and, incrementally, real syscalls
  against the real HFS+ root (`ls`/`cat`/`cd`/`pwd`/`mkdir` — required
  discovering that plain `fstat`/`getdirentries` fill an *older* struct
  layout than the `64` variants, which is what's actually needed).
  `INIT_HOST_TEST` compiles the same C logic for host-side testing without
  a kernel (`host_test.c`, `_pty_test_phase5.py` for real pty-driven
  interactive tests).
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

## Known structural gaps (context for future work, not bugs to silently fix)

- **No USB host controller driver exists anywhere accessible to this
  project.** The Pi 3's host controller is DWC2 (Synopsys USB2 OTG);
  Apple's own open-source `IOUSBFamily` only ships EHCI/OHCI/UHCI/XHCI.
  This blocks both real keyboard input (HID over USB) and Ethernet
  (`SMSC9514Ethernet` is USB-attached). Writing a DWC2 driver from scratch
  is flagged in `docs/usb-support-plan.md` as materially larger than
  everything else in this repository combined — treat as its own large,
  separately-scoped front, not something to casually pull in. Current
  console I/O is serial UART, not keyboard/HDMI.
- **Secondary CPU cores are parked below IOKit** — `BCM2837CPU` models a
  single effectively-running core; multi-core is not wired up.
- See `docs/backlog.md` for the live, prioritized (P0-P3) list of specific
  open bugs and hardware risks, and `docs/roadmap.md` for phase-by-phase
  status (kernel boot → shell → SD boot → real userland → multi-user →
  networking → self-hosting → package manager).
