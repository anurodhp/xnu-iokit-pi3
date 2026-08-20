# Historic roadmap archive: completed work

This file is a verbatim archive of the completed-work sections originally
in `docs/roadmap.md`, split out on 2026-08-17 once that file grew too large
(~3864 lines) to serve as a working plan doc. Nothing below has been
edited, summarized, or reworded from the original -- headers, dates, and
prose are unchanged, only reorganized into this file. See `docs/roadmap.md`
for the short, current-state view (active phase/step, open bugs, known
stand-ins, and condensed remaining work); use this file when you need the
underlying debugging history/evidence a status line in that shorter file is
summarizing.

Contents: Phases 1, 2, 3, and 3.5 in full (all marked done/closed in the
original text); Phase 4 in full (its own internal status notes make clear
which individual steps were done vs. still in progress at the time each
passage was written -- read `docs/roadmap.md`'s "Current status" section
for where Phase 4 actually stands today); and, from Phase 4.6, only the two
items marked `[DONE ...]` in the original (the real timezone database and
real environment-variable plumbing). Phase 4.6's remaining items (3-5:
regex, `setattrlist(2)`, Mach IPC) were explicitly "not started" in the
source text, so they are not archived here as completed work -- they're
summarized in `docs/roadmap.md`'s remaining-future-work section instead.

---

# Four-phase roadmap: kernel boot -> real SD-rooted userland

Written 2026-08-08. Each phase below is written to stand alone -- own goal,
inputs it can assume, concrete tasks, exit criteria, and open risks -- so
whoever/whatever picks up a given phase (a different agent, a different
session, a different person) doesn't need this conversation's history, only
this file plus the repo itself. Cross-phase dependencies are called out
explicitly rather than assumed.

**Parallel work note**: as of this writing, a separate effort is rebuilding
the XNU kernel itself (Phase 1) in parallel with this document being
written. Phases 2-4 are written against the current, already-working
precompiled kernel and `third_party/xnu` tree (Darwin 19.0.0 / macOS 10.15
Catalina, `config/MasterVersion`) as a concrete baseline -- if Phase 1 lands
on a different Darwin version, **re-verify every version-specific claim
below** (exact apple-oss-distributions tags, exact vnop/syscall behavior)
against whatever kernel actually ships, don't assume it carries over
unchanged. Phase 1 should record its final exact Darwin version/build
number as part of its own exit criteria so Phases 3-4 know what to pull.

---

## Phase 1: modern XNU kernel boots without panic

**Owned separately** (in progress elsewhere as of this writing) -- included
here only so Phases 2-4 have a clear, checkable gate to build on.

**Goal**: the kernel reaches a stable, non-panicking steady state (idle
thread / processor_idle) on the target (QEMU `raspi3` machine model at
minimum; real hardware via `sdcard/` is a stretch goal, not required to
unblock Phase 2).

**Exit criteria**:
- Boots to idle with no panic, on a real from-source build (not a binary
  patch of a precompiled kernel -- binary-patch boot-to-idle was already
  reached once this way, see project history, but Phase 2 onward needs a
  rebuildable kernel since Phase 2 requires a kernel *config* change).
- Exact Darwin version / `config/MasterVersion` / build identifier recorded
  somewhere durable (commit message, doc, whatever) -- Phase 4 cannot pick
  correct apple-oss-distributions tags without this.
- The toolchain that produced it is repeatable (documented build command,
  not a one-off).

**Known-working reference points to build from** (from the current tree,
may or may not carry over to a "modern" rebuild): `tools/build_xnu.sh`
(`gmake` + `build/xnu-src-build`'s hand-built `ctfconvert`/`ctfmerge`
toolchain, `KERNEL_CONFIGS=DEBUG MACHINE_CONFIGS=BCM2837`);
`third_party/qemu-zhuowei` (zhuowei's QEMU fork, generic Mach-O/ADT loader
in `hw/arm/boot.c`, `-M raspi3 -kernel ... -dtb ...`); `devicetrees/rpi3.dts`.

---

## Phase 2: shell via mockfs (not tmpfs -- see below)

**Depends on**: Phase 1's kernel, rebuildable from source with one extra
kernel-config line.

**Correction on "tmpfs"**: checked directly against this xnu source tree --
**there is no tmpfs in xnu at all** (Darwin doesn't ship a generic
in-memory filesystem the way Linux does). The real, already-implemented
mechanism for "boot to a shell with no real disk" is **`mockfs`**
(`bsd/miscfs/mockfs/`, gated by the `MOCKFS` config option, comment: "Boot
from an executable") -- it treats a `/chosen/memory-map` `RAMDisk` region as
a single Mach-O file and execs it as `/sbin/launchd`. This is a real Apple
mechanism, not something invented for this project. If a real multi-file
writable in-memory filesystem is wanted later, that's new kernel code (no
existing xnu source provides it) -- treat as a Phase 4 stretch goal, not
Phase 2.

**Status: already built** (this session) at `tools/init_binary/` --
`init.c` is a fully static, no-dyld, `LC_UNIXTHREAD` Mach-O shell/init.
Because neither `mockfs` (no `VNOP_MKDIR`/`VNOP_CREATE`) nor `devfs` (mkdir
but no regular-file content) can hold a real directory tree yet, its
`/bin /sbin /etc /dev /tmp /var` tree and all commands (`ls cat cd pwd echo
mkdir help clear exit bash`) are virtual/in-process -- read
`tools/init_binary/README.md` for the full mechanism and exact reasoning.
Logic-tested on the host via a syscall shim; never yet booted on a real
kernel (see below).

**Remaining tasks**:
1. Whatever kernel Phase 1 produces, add `mockfs` to `FILESYS_DEV` **and**
   `FILESYS_DEBUG` in `config/MASTER.arm64.bcm2837` if it isn't already
   there (already done on the current pre-Phase-1-rebuild tree; re-check
   after any tree replacement). Confirm with `nm <kernel> | grep mockfs`
   before trusting a boot test.
2. `qemu-system-aarch64 -M raspi3 -kernel <kernel> -dtb devicetrees/rpi3.dtb
   -initrd tools/init_binary/init.macho -append "rd=md0" -serial stdio`
   (`tools/init_binary/run_qemu.sh` wraps this). Confirm
   `load_init_program: attempting to load /sbin/launchd` succeeds in the
   console log, then confirm the init binary's own banner prints.
3. Debug via the same toolkit already proven on this project (`-d int`,
   `lldb gdb-remote`, static disassembly) if it panics/hangs instead.

**On "assuming we have keyboard working" -- flagging, not assuming**:
verified this is not currently true and isn't free. The Pi 3 has no PS/2;
a keyboard means USB HID, which means USB device enumeration, which means
a working USB host controller driver -- and **no DWC2 (Synopsys USB2 OTG,
the BCM283x host controller) driver exists anywhere accessible to this
project** (checked; Apple's own IOUSBFamily only ships
EHCI/OHCI/UHCI/XHCI, not the embedded-SoC DWC2 core). This is the exact
same blocker already documented for Ethernet in
`docs/sd-and-ethernet-plan.md`, and writing a USB host controller driver
from scratch is flagged there as "a materially larger undertaking than
everything else in this repository combined." **Recommendation: Phase 2's
actual input/output console is the already-working serial UART, not a
keyboard.** If real keyboard input is a hard requirement, it needs a DWC2
driver as its own separate, large, unscheduled front -- decide explicitly
whether to add that as a Phase 2b rather than silently assuming it'll
exist. That front now has its own scoped plan: `docs/usb-support-plan.md`
(written 2026-08-11, driven by Ethernet's identical DWC2 dependency below --
read that document rather than re-deriving USB scope here).

**Exit criteria**: console shows the init binary's banner and prompt;
typed input round-trips through the shell (`ls`/`cd`/`echo`/etc all work)
over whatever console is actually chosen (serial, or HDMI+keyboard if the
DWC2 work above is explicitly taken on).

**If/when the DWC2 blocker above is ever cleared, what's needed past it
for a keyboard to actually work** (recorded now so it isn't re-derived
later): once raw HID reports arrive from a transport driver, the rest of
the stack is:

1. **Report format** -- use the USB HID Boot Protocol (fixed 8-byte
   report: 1 modifier byte + 1 reserved + 6 keycode slots), which every
   USB keyboard supports as a fallback and is what BIOS/UEFI/GRUB rely on
   pre-driver-load. Skips writing a general HID Report Descriptor parser
   entirely.
2. **Keycode -> character translation** -- a lookup table from HID
   Usage Tables' Keyboard/Keypad page (0x07) usage IDs to ASCII (US-QWERTY
   simplest), plus modifier-bit handling from the boot-protocol modifier
   byte. Pure data, no real complexity.
3. **Kernel plug-in point** -- the canonical Darwin path is
   `IOHIDFamily` (IOHIDDevice -> IOHIDEventDriver -> IOHIDEvent), which
   *is* open source on `apple-oss-distributions` and directly relevant to
   this repo's own `iokit` namespace. Past that, the real stack gets
   heavy fast: `IOHIDEventSystem`, the `hidd` userspace daemon (almost
   certainly closed, same pattern as launchd/opendirectoryd), and a hard
   CoreFoundation dependency via IOKitUser.
4. **Recommendation, consistent with the launchd/opendirectoryd
   decisions in Phase 4** -- skip the full `IOHIDEventSystem`/`hidd`/
   CoreFoundation chain. Write a small custom consumer directly above the
   transport driver that does boot-protocol parsing + keycode-table
   lookup and feeds resulting characters into the same tty/console
   abstraction the shell already reads from (serial or otherwise), using
   `IOHIDFamily`'s open usage-table/report-parsing source as a reference
   only, not a build dependency.

No hidd, no CoreFoundation, no WindowServer needed -- just boot-protocol
reports -> keycode table -> tty feed, once a transport exists.

---

## Phase 3: real SD card boot

**STATUS: done, and exceeded (2026-08-09/2026-08-10)** -- all tasks below
were completed, plus more than this phase originally scoped: real
end-to-end SD block reads verified in QEMU, GPIO pinmux done on real
register writes, `"intc"` interrupt-parent wiring done, `msdosfs` mounts
the real FAT32 SD image as **root** (not just a verified-readable `IOMedia`
as this phase's exit criteria required), and `runvm.sh` now boots
unattended all the way to a real interactive shell prompt on top of that
real root. See `project_xnu_rpi3_status` memory ("SD CARD MILESTONE
REACHED", "INTERACTIVE SHELL MILESTONE REACHED") for the full writeup.

**Real ARM64 IRQ delivery through the SDHCI interrupt path -- the one open
item from this phase's original scope -- is now genuinely CONFIRMED AND
FIXED (2026-08-13), after two false starts the same day worth recording
for the methodology lesson.** First pass added
`fRealInterruptHits`/`fPollFallbackHits` counters to
`iokit/BCM2837SDHCI/BCM2837SDHCI.{cpp,h}` and reported a false-positive
"confirmed" claim caused by a real bug in the counters themselves
(credited a hit at the *consuming* side of `fPendingIntStatus` rather than
the true source -- see the file's own history/comments). Second pass
fixed that bug and re-measured correctly: `fRealInterruptHits` stayed at
**0** for an entire boot, meaning real interrupt delivery to this driver
had genuinely never been observed -- an honest "still open, root cause
unknown" result.

**Third pass found the actual root cause** by tracing the real xnu
DT-interrupt-parsing path (`IODeviceTreeSupport.cpp`'s
`IODTMapOneInterrupt()` + `IOInterruptController.cpp`'s
`registerInterrupt()`) instead of assuming: `devicetrees/rpi3.dts`'s
`intc` node used a 2-cell `{bank, bit}` interrupt encoding
(`#interrupt-cells = <2>`, `sdhci`'s `interrupts = <2 30>`), but
`IODTMapOneInterrupt()` stores a device's `interrupts` property
completely unprocessed as raw cell data, and the base
`IOInterruptController::registerInterrupt()` just takes the **first**
32-bit cell as the vector number -- nothing in generic IOKit decodes a
multi-cell `{bank,bit}` convention unless the controller subclass
explicitly overrides `registerInterrupt()` to do so, and
`BCM2837InterruptController` never did (its own header had already
flagged this exact gap as unverified before this fix). So `BCM2837SDHCI`'s
real registered vector was **2** (the bare first cell), not **62**
(bank 2 bit 30, this project's own intended `bank*32+bit` numbering) --
nowhere near where the real hardware interrupt actually lands, silently
dropped every time. **Independently corroborated the same day** by a
from-scratch adversarial review of the interrupt-controller kexts, which
reached the identical root cause by pure static analysis without seeing
this investigation -- strong convergent evidence, not just one read of
the code. That review also predicted this bug's most severe real-hardware
consequence: the wrong vector (2) is a real GPU IRQ line (System Timer
compare-match), owned by VideoCore firmware on real hardware -- enabling
it as if it were the SDHCI line would very likely have caused a real
interrupt storm on actual silicon, invisible in QEMU (which doesn't model
that timer at all). This DT bug being fixed now heads off a real-hardware
landmine, not just a QEMU-only diagnostic gap.

**Fix**: made `intc` single-cell (`#interrupt-cells = <1>`) and
pre-flattened `sdhci`'s `interrupts` to `<62>` directly, matching what
`local-intc`'s own single-cell `interrupts = <8>` already did correctly --
avoids needing to override `IOInterruptController::registerInterrupt()`
(large, sharing/locking-sensitive base-class code) at all. Recompiled the
device tree with the correct ADT-emitting `dtc`
(`third_party/dtc-AppleDeviceTree/dtc`, **not** the stock Homebrew `dtc`
that happens to be on `$PATH` -- confirmed by checking the existing
`.dtb`'s magic bytes are Apple ADT format, not standard FDT). **Re-verified
on a real QEMU boot: `fRealInterruptHits` now fires, and
`fPollFallbackHits` does not appear at all for the whole boot** -- genuine
interrupt-driven I/O, not polling, confirmed for the first time in this
project. The hybrid poll+interrupt design in `waitForIntStatus32()` is
kept anyway (real interrupts still win immediately via `IOLockWakeup()`;
costs nothing to keep as a safety net). This closes Phase 3's IRQ-delivery
item for real. The rest of this section is kept as historical record of
the original plan.

**Depends on**: Phase 1's kernel. Does *not* depend on Phase 2 (mockfs and
real-SD-root are alternative roots, not sequential -- this phase can be
worked in isolation against a Phase-1 kernel with no mockfs work present).

**What already exists** (written this session's predecessors, unverified
on real hardware): `iokit/BCM2837SDHCI` -- a real `IOBlockStorageDevice`
subclass for the Arasan SDHCI controller, SD init sequence + PIO block
read/write, interrupt-driven (not polling). Register-level behavior ported
from Linux's `drivers/mmc/host/sdhci.h`/`sdhci-iproc.c` and CSD-parsing
math verified against Linux's `mmc_decode_csd()` -- real reference sources,
not guessed.

**Concrete tasks, in the order that actually unblocks progress** (per
`docs/sd-and-ethernet-plan.md`, restated here so this phase is
self-contained):

1. **GPIO pinmux** -- nothing currently remuxes GPIO48-53 to ALT3 to route
   the Arasan controller to the physical SD slot (it defaults to onboard
   SDIO WiFi instead). Without this every SD command times out. Small,
   concrete: direct GPIO `FSEL` register pokes using macros already in
   `third_party/xnu-7195/pexpert/pexpert/arm64/BCM2837.h`
   (`BCM2837_FSEL_REG`/`BCM2837_FSEL_ALT3`), in `BCM2837SDHCI::start()` or
   earlier platform init. **This is the actual first blocker** -- do this
   before anything else in this phase.
2. **`IOStorageFamily`** is not in base `xnu` source at all (only a
   DriverKit userspace stub is) -- real `IOBlockStorageDevice`/
   `IOBlockStorageDriver`/`IOMedia`/`IOPartitionScheme` come from
   `apple-oss-distributions/IOStorageFamily` (already cloned to
   `third_party/IOStorageFamily`, gitignored). Need `IOBlockStorageDriver`
   + `IOFDiskPartitionScheme` compiled in too, to get from "one nub" to an
   actual mountable `IOMedia`. Compile-verification of this family against
   this specific old (Darwin 19) SDK has a known header-skew problem
   (`IOTypes.h` -> `mach_interface.h` -> newer Mach IPC types this xnu
   checkout doesn't define) -- same class of issue as MIG-header gaps
   elsewhere in this project; needs either a version-matched SDK or running
   xnu's own `make installhdrs`.
3. **Filesystem**: use `msdosfs`
   (`apple-oss-distributions/msdosfs`, confirmed tagged/available) over
   `hfs`. Concrete reason: `sdcard/`'s partition is *already* FAT32 (the
   Pi's GPU firmware/u-boot boot chain requires it) -- `msdosfs` needs no
   reformat and no new on-disk writer, just a reader for what's already
   physically on the card. Pull `diskdev_cmds` too for `mount_msdos`/
   `newfs_msdos`/`fsck_msdos` once there's a real userland to run them
   from (Phase 4). If Apple's real `msdosfs` source has the same
   SDK-header-skew problem as `IOStorageFamily`, FreeBSD's `msdosfs` (BSD
   licensed, same lineage xnu's own `bsd/` subtree already draws from) is
   a legitimate fallback reference/port target -- don't burn excess time
   fighting Apple's header skew if FreeBSD's version compiles cleanly
   against this tree.
4. Ethernet (`iokit/SMSC9514Ethernet`, DWC2 dependency) is explicitly
   **not** part of this phase -- separate, larger, already deprioritized
   in `docs/sd-and-ethernet-plan.md`, and formally gated on
   `docs/usb-support-plan.md`.

**Exit criteria**: a real `IOMedia` appears for the SD card's FAT32
partition, kernel can read blocks from it (verified via a known-content
file already on the card, read back correctly), and `mount()` of that
volume succeeds without a real userland process on top yet (kernel-side
verification is enough to close this phase -- wiring it into an actual
root-mount/exec path is Phase 4's job).

---

## Phase 3.5: real VFS in the shell + partition the SD card for a real permission-bearing root (2026-08-10, both 3.5a and 3.5b DONE as of 2026-08-13)

Two small, independent items, both cheap relative to Phase 4, both worth
doing before committing to Phase 4's much bigger libSystem lift.

**3.5a -- make the current shell's filesystem commands real. DONE
(2026-08-13).** `tools/init_binary/init.c`'s `ls`/`cat`/`cd`/`pwd`/`mkdir`
now issue real `open`/`read`/`close`/`fstat64`/`getdirentries64`/`mkdir`
syscalls against the actually-mounted real HFS+ root, replacing the old
entirely-fake in-memory `nodes[]`/`vfs_init()`/`vfs_resolve()` tree
(confirmed gone: `ls`/`cd` no longer know the string "bin" or "etc" at
all). `cd`'s cwd tracking stays a plain local absolute-path string (no
raw `getcwd`/`chdir` syscall exists at this Darwin vintage --
`getcwd()` is a Libc-level directory walk via `getattrlist`, not a
syscall -- so each `cd` is instead validated by a real `open()`+`fstat64()`
against the target before accepting it).

**Two real, non-obvious ABI bugs found and fixed the same way** (raw byte
dumps of what the kernel actually returned, not assumption -- see
`init.c`'s own comments on `sys_fstat`/`sys_getdirentries` for the full
evidence): the plain `fstat` (189) and `getdirentries` (196) syscalls do
**not** fill the modern 64-bit-ino `struct stat`/`struct dirent` layouts --
those are separate syscalls, `fstat64` (339) and `getdirentries64` (344),
cross-checked against `bsd/kern/syscalls.master`'s `stat`/`stat64` and
`getdirentries`/`getdirentries64` pairs. Using the plain (non-64) numbers
silently filled a different, older struct shape, corrupting every
`st_mode` read (root's real mode `0040775` read back as a bare `2`, the
raw `VDIR` vtype value bleeding through) and every `d_reclen`/`d_namlen`
walk -- explains why every directory looked like "not a directory" and
`ls` either printed nothing or spun through garbage until this was found.
Switched to the `64` syscall numbers; `struct kstat`/`struct kdirent` in
`init.c` (verified byte-for-byte against a real host arm64 Darwin
compiler's actual `<sys/stat.h>` offsets) were correct all along.

**Verified via a full real-QEMU-boot pty test** against the real HFS+
root (own scratch SD-image copy, not the shared one): `ls`/`ls <path>`
correctly list real directory contents (root's real `dev`/`sbin`
subdirectories, `/sbin`'s real `launchd`); `cd`/`pwd` correctly
navigate and report the real cwd; `cat` correctly errors on a missing
file and on a real directory (`/sbin: is a directory`). `mkdir` reaches
a real, understood failure (`errno 30`/`EROFS`) rather than a bug --
this project's real HFS+ root is apparently still mounted read-only
(plausibly never exercised by a real write before this), a kernel/mount
matter outside this file's scope, not an `init.c` bug. One remaining
cosmetic-only gap: HFS+'s private metadata *file* (as opposed to the
private metadata *directory*, which is filtered) shows in `ls` with a
garbled name -- documented as a known gap in `init.c`, doesn't affect
any real file/directory listing.

The old "`ls /` crashes after the first entry" bug mentioned in
`docs/backlog.md` item 22 was **not reproduced** with the new
real-syscall implementation (`ls` now reliably lists every entry) --
plausibly that old crash was specific to the fake tree's own bug, now
moot since that code path no longer exists; not independently
re-diagnosed against the old code, since it's gone.

**3.5b -- two-partition SD card, HFS+ root. DONE (2026-08-10).** Resolves
the open decision in `docs/multiuser-userland-plan.md` §0 (no on-disk Unix
permission bits exist on FAT32 at all -- an architectural blocker for real
multi-user, independent of how complete the userland ends up being).
**Decision**: a small first partition stays FAT32 (all the Pi GPU
firmware/u-boot chain actually needs is *a* FAT32 partition to find the
boot files, not the whole card -- standard Raspbian-style
`boot=FAT32 / root=<real fs>` split), second partition is `hfs`. `hfs`
(`bsd/hfs`) already compiled cleanly into the kernel (`FILESYS_DEV`/
`FILESYS_DEBUG`) before this pass -- no new kernel-side filesystem driver
work was needed. What this pass did:
1. Vendored `IOFDiskPartitionScheme.cpp`/`.h` and `IOPartitionScheme.cpp`/
   `.h` from `third_party/IOStorageFamily` into
   `third_party/xnu-7195/iokit/Storage`+`iokit/IOKit/storage` (same
   pattern as the existing `IOMedia`/`IOBlockStorageDriver`/
   `IOMediaBSDClient` vendoring) and added both to `iokit/conf/files`.
2. Added an `IOFDiskPartitionScheme` personality in
   `iokit/BCM2837ArmIO/BCM2837Personalities.cpp`, matching generically
   against any `IOMedia` (declines on its own via a real MBR-signature
   check in its `probe()`, so it's safe alongside the existing
   `msdosfs`-superfloppy boot path). Real bug found+fixed along the way:
   without an explicit `IOMatchCategory`, this personality and the
   existing `IOMediaBSDClient` one both fell into IOKit's shared default
   match category for the same provider, so only one of them was ever
   actually `start()`'d (`IOService::copyClientWithCategory()`'s
   exclusivity check) even though `IOFDiskPartitionScheme::probe()`
   itself succeeded (confirmed via temporary `IOLog` instrumentation,
   since reverted). Fixed by tagging the `IOFDiskPartitionScheme`
   personality with the real, vendored `kIOStorageCategory` ("IOStorage")
   match category, exactly as real apple-oss-distributions storage
   drivers do (`iokit/IOKit/storage/IOStorage.h`) -- `IOMediaBSDClient`
   stays in the default category since it isn't a storage-interpreting
   driver, so the two no longer collide.
3. Also hit and fixed a real, pre-existing, unrelated build-system bug
   blocking any full `make all` on this tree: `config/Private.arm64.exports`
   (real, unmodified Apple file) declares exports for Apple Silicon
   (T8020/M1) DART/SART/UAT IOMMU symbols (`pmap_iommu_*`,
   `IOUnifiedAddressTranslator`, etc.) that this public xnu-7195.81.3
   source drop never actually ships an implementation for -- confirmed by
   reverting all local changes and reproducing the same
   `check_all_exports` failure on a stock checkout. Commented out those
   specific declarations (same closed-source-gap class as the earlier
   pthread.kext/SHA2 stand-ins).
4. Built a real 2-partition 4 GB test image (`qemu/hfs_root_test.img`,
   gitignored): MBR partition 1 = 64 MB FAT32 boot (bootcode.bin/
   start.elf/fixup.dat/config.txt/rpi3.dtb/kernel.img, for real-hardware
   parity -- QEMU itself boots via `-kernel`/`-dtb` and doesn't read
   this), partition 2 = HFS+ root (`/sbin/launchd` = the init binary,
   `/dev` = devfs mount point, mirroring the existing msdosfs root
   layout). Built via `hdiutil attach -imagekey diskimage-class=CRawDiskImage`
   + `diskutil partitionDisk` so the underlying file stays a flat raw
   image (no UDIF wrapper), directly usable by QEMU's
   `-drive ...,format=raw`. One real gotcha hit and resolved: a first
   512 MB attempt put QEMU's emulated SD card into SDSC (CSD v1) mode,
   which `BCM2837SDHCI` deliberately doesn't support (SDHC/SDXC only) --
   fixed by sizing the image at 4 GB, matching the existing working
   images.
5. Verified via QEMU boot (`runvm.sh`, now defaulting to this image):
   `IOFDiskPartitionScheme` publishes `disk0s1`/`disk0s2`,
   `rd=disk0s2` resolves as `BSD root: disk0s2, major 14, minor 2`,
   `vfs_mountroot "hfs" result=0` / `vfs_mountroot() succeeded`, and the
   interactive shell reaches `sh:/ %` and accepts typed input, all from
   the real HFS+ root partition.
6. **Not done**: applying the same 2-partition layout to `sdcard/`'s
   real-hardware image (still the single flat FAT32 image) -- real
   hardware boot is separately blocked on no serial cable per existing
   project memory, so this was deprioritized; `qemu/hfs_root_test.img`'s
   FAT32 partition 1 already contains everything needed to build that
   real-hardware image the same way once useful.

**Exit criteria**: `ls`/`cat`/etc. in the shell reflect the real,
currently-mounted filesystem's actual contents (3.5a, done); a
2-partition SD card (FAT32 boot + `hfs` root) boots and mounts `hfs` as
root in QEMU (3.5b, done). Both met.

---
## Phase 4: real userland -- filesystem, tools, PID 1

**STATUS 2026-08-13: step 2 (PID 1) done and confirmed on real QEMU boot**
(`tools/init_binary/init.c`'s `init_main()` -- real `fork`/`wait4`/`execve`,
respawn-on-exit verified across multiple cycles; see project memory
`project_xnu_rpi3_userspace_libc_dyld.md`). Steps 1/3/4 (libSystem pull,
real commands, Libinfo resolver) below are **in progress, not done**:

- **The exact Darwin version is 20.3.0, not 19** (this phase's original
  text below assumed Darwin 19/Catalina from an earlier, since-deleted
  kernel tree -- `third_party/xnu-7195/config/MasterVersion` says 20.3.0,
  i.e. macOS 11.2 Big Sur; re-verify again if the kernel tree ever moves).
- **The "no cross-project version-manifest API exists" claim below is
  wrong** -- `apple-oss-distributions/distribution-macOS` is exactly that
  manifest: it's a superproject repo with one tag per macOS release
  (`macos-112` = macOS 11.2, confirmed by its pinned `xnu` submodule commit
  `e890dd3c` matching `xnu-7195.81.3^{commit}` exactly) whose `.gitmodules`
  + submodule gitlinks pin the exact commit of every other
  apple-oss-distributions repo Apple shipped together for that release.
  `git ls-tree macos-112 -- <name>` gives an exact commit for any repo this
  phase still needs; cross-reference against that repo's own tags to get a
  human-readable tag name. Use this for anything still missing below
  instead of date-proximity guessing.
- **All of step 1's repos, plus all of step 3's `bash`/coreutils repos and
  `Csu`, are now cloned** at their exact `macos-112`-matched tags -- see
  `setup_third_party.sh`, which has the full pinned list and the reasoning
  above in its header comment.
- **Csu verified**: `tools/userland_staging/build_crt0_smoketest.sh` links
  a trivial raw-syscall `main()` against Apple's real, unmodified `Csu`
  (`start.s`/`crt.c`, `-static`/no-`__DYNAMIC__` path) with zero patches
  needed -- produces a structurally-correct freestanding Mach-O
  (`LC_UNIXTHREAD`, zero `LC_LOAD_DYLIB`), confirmed via `otool -l` and a
  clean link (no undefined symbols beyond `_main`/`_exit`). Real Apple
  source is a drop-in replacement for `tools/init_binary/init.c`'s
  hand-written entry point.
- **`libsyscall` verified working (2026-08-13), real Apple stub generator,
  not hand-written asm.** `Libc` itself (`third_party/Libc`) is an
  Xcode-project build (`Libc.xcodeproj`, no plain `Makefile`) -- not
  directly driveable on this host (see `tools/build_kexts.sh`'s header
  comment: `xcodebuild` itself crashes on this machine, a CoreChart.framework
  problem, unrelated to Libc). The syscall-stub layer is different: it's
  `libsyscall/` inside `third_party/xnu-7195` (not a separate repo, already
  arm64-ready), and its generator is plain Perl + `clang -c`, no Xcode
  needed. Ran it for real:
  `ARCHS=arm64 perl third_party/xnu-7195/libsyscall/xcodescripts/create-syscalls.pl
  third_party/xnu-7195/bsd/kern/syscalls.master
  third_party/xnu-7195/libsyscall/custom third_party/xnu-7195/libsyscall/Platforms
  iPhoneOS <outdir>` produces real per-syscall `.s` files. Their arm64
  macro expansion (`SYS.h`'s `DO_SYSCALL`) is `svc #0x80` /
  `b.cc 2f` / `bl _cerror` -- an **independent, from-Apple's-own-tooling
  confirmation** of the exact carry-flag-means-error convention
  `tools/init_binary/init.c` had to reverse-engineer by hand from reading
  `osfmk`/`bsd` source directly.
  `tools/userland_staging/build_libsyscall_smoketest.sh` regenerates
  `getpid`/`write`/`exit` stubs fresh from that generator every build,
  assembles them with plain `clang -target arm64-apple-macos11`, and links
  them against real `Csu` crt0 plus a new minimal `cerror`/`errno` stand-in
  (`cerror_stub.c` -- Apple's real `errno.c` needs `_os_tsd_get_direct()`,
  which reads a per-thread TSD block via `TPIDRRO_EL0` that only exists
  once `libpthread`'s `bsdthread_create()` has run; this target has no
  pthread bootstrap at all yet, so using the real one as-is risks reading
  an unset/garbage register and dereferencing it -- same "stand-in for a
  subsystem this target doesn't have yet" pattern as `bsd_pthread_stub.c`
  on the kernel side, not a hack). Result: a fully static, zero-undefined-
  symbol, `LC_UNIXTHREAD`/no-`LC_LOAD_DYLIB` Mach-O built entirely from
  real Apple `Csu` + real Apple-generated syscall stubs, confirmed via
  `nm`/`otool -l`. Not yet boot-tested in QEMU (out of scope for the
  session that did this -- next step for whoever picks this up).
- **`Libc` proper (the large remaining item) still not attempted** --
  confirmed harder than just "run make": no plain build system to drive
  outside Xcode. Realistic path forward is almost certainly compiling a
  curated subset of `.c` files directly with `clang` (same spirit as this
  project's kext/kernel builds, which also bypass `xcodebuild`) rather than
  fighting `Libc.xcodeproj` itself -- expect the same header-skew class of
  bug this project hit with `IOStorageFamily`/`hfs`, likely still a
  multi-session effort.
- **Not attempted**: step 4 (Libinfo resolver) -- correctly low-priority
  per the plan below, blocked on step 1 landing first regardless.

**Depends on**: Phase 3 (needs the real mountable SD volume to root from)
and, for anything beyond the current mockfs shell's capability, on pulling
real dyld/libSystem. Does not depend on Phase 2 surviving -- Phase 2's
mockfs/virtual-vfs init is a bootstrap scaffold this phase fully replaces,
not a component it builds on. Does not strictly depend on Phase 3.5, but
should follow it -- no point building real coreutils against a root
filesystem that's about to be re-partitioned/reformatted underneath them.

**Plan already decided** (carried over from prior planning, restated here
for isolation): **launchd is not available at this ABI** -- last public
open-source tag is `launchd-842.92.1`, 10.9-era, predating the libxpc
merge; no version-matched launchd exists for Darwin 19 (re-check this
specific claim if Phase 1 lands on a different Darwin version -- it may
have changed). Likewise **opendirectoryd is closed-source**, no repo
exists. Concrete substitution plan, in order:

1. **Pull the libSystem stack** from `apple-oss-distributions` at tags
   matching Phase 1's exact Darwin version (use commit-date proximity to
   that version's `xnu` tag -- no cross-project version-manifest API
   exists, checked): `dyld`, `Libc`, `libpthread`, `libplatform`,
   `libdispatch` (Grand Central Dispatch), `libclosure` (Blocks runtime,
   `libdispatch` depends on it), `libmalloc`, `libunwind`, `Libinfo`,
   `removefile`/`copyfile`. `Libsystem` itself is the umbrella
   Makefile-only project showing how these bundle into `libSystem.dylib` --
   read it as the assembly reference, not a component to port.
2. **Custom PID-1, modeled on/ported from FreeBSD's `init`** (fork/exec/
   wait/reap, single-user -> multi-user via `/etc/rc`) instead of
   launchd -- launchd's job-activation model is deeply Mach-port/XPC
   specific and not a portable target; FreeBSD's is a plain BSD process
   model, a much closer structural fit, and BSD-licensed (xnu's own `bsd/`
   subtree is already FreeBSD-lineage, so this isn't an odd import). This
   replaces `tools/init_binary/`'s virtual shell as PID 1 once dyld/Libc
   are in place.
3. **Real commands**, from `apple-oss-distributions` at the same
   Darwin-matched tags: `bash` (Apple's actual GPLv2 bash 3.2, frozen
   there since ~2007 -- real bash, not Phase 2's builtin fake), plus
   `shell_cmds`/`file_cmds`/`text_cmds`/`adv_cmds`/`system_cmds`/
   `basic_cmds`/`developer_cmds`/`misc_cmds` for the rest of a normal BSD
   command set. Also need `Csu` (the crt0-equivalent startup glue real
   dyld-linked executables require before `main()` runs -- Phase 2's
   binary skipped this entirely via a hand-written `LC_UNIXTHREAD` entry,
   which stops being an option once linking against real dyld/Libc).
4. **Custom local-only user resolver instead of opendirectoryd**: pull
   `Libinfo` (open, actively tagged) at the same matched tag, and behind
   it write a minimal flat-file resolver modeled on FreeBSD's
   `/etc/master.passwd`+`pwd_mkdb` model (`getpwnam`/`getpwuid` reading a
   local hashed db directly, no daemon/RPC) rather than reconstructing
   opendirectoryd's real XPC protocol. Only actually needed once real
   multi-user semantics, `~` expansion by name, `id`/`whoami`, or a login
   prompt are wanted -- not needed just to get a root shell running.
5. **Root filesystem**: whatever Phase 3 lands (real SD + `msdosfs`, or
   `hfs` if that path was taken instead) becomes the real root -- this is
   what finally retires `mockfs`/the virtual vfs from Phase 2 entirely.

**Exit criteria**: kernel mounts the real SD-card filesystem as root, execs
the custom FreeBSD-derived PID-1 from it (not from a `mockfs` RAMDisk),
which brings up a real dyld-linked `bash` with real `ls`/`cat`/etc as
separate executables read from the actual SD card -- i.e., everything
Phase 2 faked, now real and running off real storage.

**Progress 2026-08-13 (third Phase 4 pass): a real `libc_string.a` builds
and links, first genuine `Libc` progress beyond `Csu`/syscall stubs.**
Surveyed `third_party/Libc`'s layout and confirmed a real gap: this
checkout has **no portable-C implementation of `memcpy`/`memset`/`strlen`/
`memcmp`/`strcmp`/`strncmp`/`bcopy`/`bzero`/`memmove` for any
architecture** (only `x86_64`/`i386` `mcount.s` profiling stubs exist, no
arm64 source at all) -- confirmed by grep before falling back, per this
project's standing Apple-first/FreeBSD-fallback rule (see
`feedback_source_priority_apple_freebsd_custom` memory). Filled that gap
with real FreeBSD source (fetched fresh by
`tools/userland_staging/build_libc_string.sh`, not vendored -- the
classic Chris Torek `bcopy`/`memset` reference implementations), and built
**20 real Apple `Libc` string functions** (`third_party/Libc/string/` +
`string/FreeBSD/`) on top: `strdup`, `strndup`, `strtok`/`__strtok_r`,
`strsep`, `strpbrk`, `strcspn`, `strspn`, `strnstr`, `strmode`, `strerror`/
`strerror_r`, `rindex`, `rpmatch`, `swab`, `timingsafe_bcmp`, `stpcpy`/
`stpncpy`, `strcat`/`strncat`, plus the 9 FreeBSD-filled primitives above.

Two real, minimal, documented fixes needed to get there (see
`build_libc_string.sh`'s own header comment for the full reasoning, not
repeated here): `-D_LIBC_NO_FEATURE_VERIFICATION` (Apple's own escape
hatch for a generated `libc-features.h` this project's non-Xcode build
never produces) and `-D_FORTIFY_SOURCE=0` (Apple's headers turn FORTIFY on
by default, which is correct for *callers* of e.g. `stpcpy` but breaks
compiling `stpcpy`'s own implementation, since its definition gets
macro-expanded into a call to itself). Plus one genuinely hand-written
shim (not a stub of real logic): a 3-line `sys/limits.h` defining
`LONG_BIT=64`, a BSD-ism absent from Apple's public headers that a couple
of FreeBSD source files expect -- fixed at 64 since arm64 is strictly
LP64 for every target this project builds.

**Verified, not just claimed**: linked `tools/userland_staging/libc_string_smoketest.c`
(calls `bzero`/`strcat`/`strlen`/`strcmp`/`memcpy`/`strncmp`/`stpcpy`/
`strtok` for real) against real `Csu` crt0 + `libc_string.a` --
`nm -u`/`otool -l` confirm **zero undefined symbols**, still a fully
static/dyld-free `LC_UNIXTHREAD` Mach-O.

**Tier boundary found and correctly NOT pushed past**: the next ~35
`Libc` string/locale/wide-char files (`strcasecmp`, `strcoll`, `strsignal`,
all `wcs*`/`wmem*`) all cascade into `xlocale_private.h` ->
`pthread/tsd_private.h` -- real per-thread-storage APIs
(`_pthread_key_create`/`_pthread_getspecific`/etc.) that don't exist
without a real `libpthread` bootstrap. Same class of dependency the prior
Phase 4 pass already hit and correctly deferred for `cerror_stub.c`'s
per-thread `errno`. **Not shimmed around** -- this is a real, correct
place to stop and hand off, not a gap to paper over. `strdup`/`strndup`
also won't fully *link* into a real program yet (they call `_malloc`,
still Tier 2/unbuilt) even though they compile standalone today.

**Next steps, in order**: (1) simple `stdlib`/`stdio` functions with no
syscall or TSD dependency (a numeric-parsing/`qsort`-class tier, not yet
surveyed), (2) a real heap allocator (`malloc`/`calloc`/`free` -- unlocks
`strdup`/`strndup` fully linking, plus is a hard prerequisite for `stdio`).

**CORRECTED 2026-08-13, same day: the "hard KERNEL wall" conclusion just
above was WRONG**, caught by adversarial review -- `bsdthread_register`
really is stubbed to `ENOTSUP` as found, but it is **not the only path**.
A second, completely separate, **already fully real (non-stub)** kernel
mechanism sets `TPIDRRO_EL0`: userspace issues a `platform_syscall` trap
(`x3`=2 "set cthread self", `x0`=tsd_base, `x16`=`PLATFORM_SYSCALL_TRAP_NO`
=`0x80000000`, `svc #0x80` -- a completely different trap path from the
regular BSD syscall convention, dispatched by `osfmk/arm64/sleh.c`'s
`handle_svc()` *before* it ever reaches the pthread stub table) ->
`osfmk/arm64/machine_routines.c`'s `platform_syscall()` case 2 ->
`thread_set_cthread_self()` -> `machine_thread_set_tsd_base()`
(`osfmk/arm64/pcb.c`), which really does set `thread->machine.cthread_self`
and, for the calling thread, `TPIDRRO_EL0` itself immediately via a real
`msr` instruction -- confirmed by reading `pcb.c` directly, not assumed.
`osfmk/arm64/cswitch.s` reloads it from `thread->machine` on every future
context switch. Every ABI detail needed to call this from userspace was
traced to source, not guessed: the 8-byte alignment requirement
(`MACHDEP_CPUNUM_MASK`, `osfmk/arm64/machine_machdep.h`), and the real TSD
block layout real Apple code expects (`_PTHREAD_STRUCT_DIRECT_TSD_OFFSET`
= 224 bytes on LP64, `third_party/libpthread/private/pthread/private.h` --
`TPIDRRO_EL0` masked points at `tsd[0]`, *not* the struct base).

**Implemented and self-adversarially-reviewed** (`tools/userland_staging/tsd_bootstrap.c`):
the ~20-line bootstrap (allocate an aligned TSD block, one
`platform_syscall` call), plus an adapted `pthread_key_create`/
`pthread_setspecific`/`pthread_getspecific` faithfully following
`third_party/libpthread/src/pthread_tsd.c`'s real algorithm (same
constants: `__pthread_tsd_first`=10, `_start`=256, `_end`=768, all
cross-checked against real source) on top of real, self-contained
`_os_tsd_get_direct`/`_os_tsd_set_direct` primitives
(`third_party/xnu-7195/libsyscall/os/tsd.h` -- the same primitives real
`errno.c` uses). One deliberate, clearly-flagged simplification: the real
`os_unfair_lock` around key-table access is omitted, since this project's
target is genuinely single-threaded (no concurrent access is possible to
protect against, so the real algorithm's *correctness* doesn't depend on
it here, only its safety under contention that cannot occur). Self-review
before committing caught and fixed two real issues: a destructor-marker
value that didn't match the real algorithm's semantics for low/reserved
keys (inert for this file's own test, fixed anyway since this is meant to
be faithful, not "passes by accident"), and a build-script exit-code bug
that would report false failure on success.

**CONFIRMED FOR REAL via QEMU boot-test (2026-08-13, same day)**: clean
compile, link with zero undefined symbols, static/dyld-free
`LC_UNIXTHREAD` Mach-O (as before) -- and now genuine execution
confirmation too, closing the gap the prior pass correctly stopped short
of. Added a small `open_console_if_needed()` (mirrors `init.c`'s own
`/dev/console` dance -- needed because a binary run directly as PID 1
inherits an empty fd table) so this smoketest is self-verifying when
booted standalone, then injected `tsd_bootstrap.macho` as `/sbin/launchd`
on a scratch copy of the SD image (not the shared golden one) and booted
it for real. Console output, verbatim:
```
tsd_bootstrap: platform_syscall confirms TPIDRRO_EL0 base set
tier A: _os_tsd_get_direct/_os_tsd_set_direct round-trip a real value
tier B: pthread_key_create/setspecific/getspecific work: real independent TSD slots
tsd_bootstrap smoketest: all real
```
followed by a clean `exit status 0` (the subsequent
`panic(...): initproc exited` is the expected, harmless "PID 1 returned
with nothing to take over" case, not a crash). Real TSD is genuinely
working end to end: the `platform_syscall` bootstrap, the real
`TPIDRRO_EL0` round-trip, and two independent `pthread_key_create`-derived
slots not aliasing each other -- all confirmed on-target, not just
linked. **This directly unblocks the ~35 locale/wide-char/thread-safe-
`strerror` Libc files found gated on TSD** -- that's the real next step,
not further TSD verification.

**Fourth pass, same day: resumed the ~35 gated files -- 109 real Libc
locale/string files compiled (up from 20), real TSD wired in as their
actual dependency, and a new (non-TSD) wall found.** Real, correct
findings from this pass are kept below (the build-config discoveries all
held up); its overclaims were caught by adversarial review and corrected
in the fifth pass further down -- **do not trust "verified"/"exercises
the full real chain" language from this pass in isolation, read the fifth
pass for what was actually true.**

Getting real `third_party/Libc/locale/` source building at all needed
**three distinct, real, non-obvious build-configuration fixes**, none of
them guessed:
1. **`-D__LIBC__`**: Apple's own real "I am building Libc itself" flag.
   Without it, `collate.h`'s `#ifdef __LIBC__`-guarded prototypes
   (`__collate_mbstowcs`/`__collate_wcsdup`) are invisible at their call
   sites, so the compiler synthesizes a wrong implicit declaration and
   then fails with "conflicting types" once it reaches the real,
   correctly-typed definition later in the same file. Root-caused by
   reading the exact "conflicting types" vs "previous **implicit**
   declaration" compiler note, not assumed.
2. **`-D__ct_rune_t=__darwin_ct_rune_t`**: this SDK only exposes the
   Darwin-renamed public name (both `int` on arm64); a few FreeBSD-lineage
   files (`runetype.c`/`tolower.c`/`toupper.c`) reference the original
   private FreeBSD name directly -- a naming-convention gap between the
   two lineages, not a real type mismatch.
3. **Host SDK skew, same class as Phase 4's earlier `Csu`/libsyscall work
   but hitting it via a different, deeper path this time** (`stdlib.h` ->
   `malloc/_malloc_type.h`'s `__API_AVAILABLE` macros expecting newer
   OS-version arguments than this project's target line ever hits): fixed
   the same way `tools/build_kexts.sh` already does for the IOKit kexts --
   Xcode 12's iPhoneOS SDK instead of the host's modern default one, since
   Xcode 12 (~2020) is contemporaneous with this project's Darwin-20.3
   target. iOS vs macOS doesn't matter for this pure-C code (Libc's own
   source already branches on `TARGET_OS_IPHONE` internally).

**A fourth, non-obvious linking-only requirement**: once mixing object
files built with the iPhoneOS-SDK toolchain (locale tier) and the host's
default toolchain (string tier, TSD shim, `Csu`), `ld` refuses outright
("building for iOS, but linking in object file built for macOS") --
Mach-O's platform load command is enforced at link time, not just a
warning. Fixed by rebuilding everything going into one final link with
the *same* toolchain/target consistently, not mixed. Also found and fixed
a real functional bug in the process: initially linked `apple_locale/*.o`
as a **raw glob** on the command line instead of through a proper `.a`
archive -- with raw `.o` files every undefined symbol in every file must
resolve unconditionally; through an archive, the linker only pulls in
members actually referenced. Using the archive properly cut the required
symbol set drastically.

**Wall found this pass, distinct from TSD (status as of the fourth pass --
see the fifth pass immediately below for how this actually resolved)**:
`___global_locale`/`___locale_key`/`__c_locale` (what `strcasecmp`'s real
locale-aware path needs) all live in one translation unit, `xlocale.c` --
once any symbol from it is pulled in, *all* of its own internal helper
functions' undefined references must also resolve, even ones the calling
smoketest's code path never actually reaches at runtime (static-linking
granularity, not a logic error). That pulls in
`setlocale.c`/`collate.c`/`rune.c`/`ldpart.c`'s real
locale-**file**-loading machinery, which needs **real stdio**
(`fopen`/`fclose`/`fread`/`fileno`/`rewind`) -- at the time, believed to
also need real POSIX regex (`regcomp`/`regexec`/`regfree`); the fifth pass
below found the actual `strcasecmp` link path doesn't reach regex at all.
Raw syscalls (`open`/`read`/`write`/`close`/`fstat`, real numbers
cross-checked against `syscalls.master` same as `init.c`),
`___error`/errno (`tools/userland_staging/cerror_stub.c`, already
existed), and a handful of simple environment/string-function gaps
(`getenv`/`issetugid`/`getprogname`/`isascii`/`isdigit`/`memchr`/`strchr`/
`strcpy`/`strncpy`/`strlcpy`/`strlcat`/`strnlen`/`ffs`, real FreeBSD
source for the string ones matching the established pattern) were all
resolved along the way.

See `project_xnu_rpi3_userspace_libc_dyld.md` memory for the same
findings with more provenance detail, plus a brief `Libinfo` survey for
Phase 5 (kept separate, not part of Phase 4's critical path).

**Fifth pass, same day: adversarial review of the fourth pass's commit
(`9a2ec1d`) found real bugs in code that had been called "verified" --
every one fixed, then the full chain re-linked for real with zero
undefined symbols.** Findings and fixes, in order of severity:

1. **`pthread_once` treated the real 16-byte `pthread_once_t` as a plain
   `long`.** Real `PTHREAD_ONCE_INIT` sig is `0x30B1BCBA` (from the SDK's
   `pthread_impl.h`), never 0, so the old `== 0` check never fired and
   `init_routine()` silently never ran -- a correctly-linking function
   that was dead code at runtime. Fixed with the real
   `struct { long __sig; char __opaque[8]; }` layout and a sentinel (1)
   distinct from both BSS-zero and the real init signature.
2. **Lazy bootstrap doesn't cover real Libc's fast TSD path.**
   `_pthread_getspecific_direct()` -> `_os_tsd_get_direct()` reads
   `TPIDRRO_EL0` directly, bypassing this shim's own functions entirely,
   so first-call lazy bootstrapping never triggers for that path. Fixed
   by requiring eager bootstrap (`pthread_tsd_shim_bootstrap()`) as
   literally the first line of `main()` in both smoketests -- not
   optional, not lazy.
3. **`__xlocale_init()` was never called.** `__locale_key` stayed at
   `(pthread_key_t)-1` -- meaning all locale-TSD reads/writes targeted
   slot -1 (8 bytes before the real block) and only "worked" because that
   memory happened to be zero. Fixed via a new
   `pthread_tsd_shim_libc_bootstrap.c` (`pthread_tsd_shim_bootstrap_for_libc()`)
   that calls the real, traced-safe `__xlocale_init()` explicitly after
   TSD bootstrap.
4. **Cross-toolchain platform-tag mismatch.** Some files were still built
   with the host's default `-target arm64-apple-macos11` while the locale
   tier used Xcode 12's `-target arm64-apple-ios14.4`; `ld` refuses
   outright ("building for iOS, but linking in object file built for
   macOS") -- Mach-O's platform load command is enforced at link time.
   Fixed by rebuilding *everything* in `build_libc_locale.sh` (string
   tier, locale tier, TSD shim, compat stubs, syscalls, stdio) with the
   same toolchain.
5. **Real files that were never wired into any build script** --
   `pthread_tsd_shim_smoketest.macho` had no script at all. Added
   `build_pthread_tsd_shim_smoketest.sh` (and, this pass, fixed a real bug
   in *that* new script: it never `mkdir -p libc_build` before writing
   into it).
6. **`compat_stubs.c`'s `realloc()` had a real OOB read** -- it copied the
   NEW (requested) size from the OLD block unconditionally, reading past
   the end of the old allocation whenever growing. Fixed by storing each
   block's real size in a header at alloc time and copying
   `min(old_size, new_size)`.
7. **`-w` was hiding real, `-Werror`-class implicit-function-declaration
   and incompatible-pointer-type errors** for `_pthread_key_create`/
   `_pthread_once`/`_pthread_getspecific`, `_getprogname`/`_write`,
   `_fstat`/`_read`/`_close`, `_open`. Removed `-w`, added a real,
   force-included prototypes header (`libc_build_prototypes.h`) --
   surfaced (and fixed) a genuine bug this uncovered: `pthread_key_t` is
   `unsigned long` (8 bytes), but the shim's key parameters were `int`/
   `int*`, silently truncating every key write to 4 bytes.
8. **`__LIBC__`'s side effects weren't fully explored** -- it also gates
   `xlocale/_ctype.h` from providing inline `ctype_l` functions to
   requiring real externalized definitions from
   `third_party/Libc/locale/isctype.c`. Wired that file in, and removed
   this project's own hand-written `isascii`/`isdigit` stand-ins (now a
   real duplicate-symbol conflict with `isctype.c`'s definitions).

**Then, with all eight fixed, the actual stdio wall flagged in the fourth
pass above was closed for real** -- not stubbed. The real call sites
(`collate.c`/`rune.c`/`setrunelocale.c`) turned out to need exactly 5
stdio functions: `fdopen`/`fread`/`fileno`/`rewind`/`fclose`. New file
`libc_locale_stdio.c` implements all 5 as a minimal, real, opaque-handle
subset (`struct __libc_locale_FILE { int fd; int eof; int error; }`,
deliberately typed `void*` at the interface to avoid colliding with real
`stdio.h`'s own `FILE` typedef, since every real caller only ever passes
it opaquely -- confirmed via grep before choosing this), built on the
project's own `_read`/`_close` raw syscalls plus a new real `lseek`
syscall (`SYS_lseek`=199, cross-checked against `syscalls.master`) for
`rewind()`. Regex was never actually needed by this link path --
`strcasecmp`'s real dependency chain doesn't reach `regcomp`/`regexec`.

**Result, verified this pass by two clean full rebuilds from a deleted
`libc_build/`, not assumed:** `pthread_tsd_shim_smoketest.macho` and
`libc_locale_smoketest.macho` both link with **zero undefined symbols**
(`nm -u`, empty output), no `LC_LOAD_DYLIB` (still fully dyld-free), and a
present `LC_UNIXTHREAD` entry point. 113 real Apple/FreeBSD Libc
locale/string files plus the full stdio subset all compile and link
clean under one consistent toolchain. **What's still open, honestly**:
true *runtime* correctness (does `strcasecmp`'s locale-aware path actually
produce the right answer under QEMU) is unverified -- QEMU boot-testing is
out of scope for this pass, reserved for the coordinator/parent session.
`strtol`, `sys_errlist`/`sys_nerr`, `strftime_l`/`iswspace_l`
(wide-char-adjacent), and a public `tolower_l`/`toupper_l` name (the real
internal `__tolower_l`/`__toupper_l`, double-underscore, exist and compile
in `third_party/Libc/locale/FreeBSD/nomacros.c`, but no bare POSIX xlocale
name calling them was found under that exact name in this checkout) remain
unaddressed -- not on the critical path for the `strcasecmp` smoketest,
flagged rather than guessed at.

**Sixth pass, 2026-08-14: real QEMU boot verification of both smoketests
(closes the "runtime correctness unverified" gap the fifth pass left
open).** Added a real `exec <path> [args...]` builtin to
`tools/init_binary/init.c`'s shell (`cmd_exec()`) -- fork + execve + wait4
a real on-disk binary and decode its real BSD wait status
(`bsd/sys/wait.h` encoding: low 7 bits 0 means exited normally, next byte
is the real exit code), since nothing in this shell could previously run
anything off disk besides `/sbin/launchd` itself. Copied
`pthread_tsd_shim_smoketest.macho` and `libc_locale_smoketest.macho` onto
`qemu/hfs_root_test.img`'s real HFS+ root at `/sbin/tsd_smoketest` and
`/sbin/locale_smoketest` (same `hdiutil`/`diskutil` mechanism
`tools/init_binary/inject_into_sd_image.sh` already uses for
`/sbin/launchd`), then drove a real interactive QEMU boot
(`tools/init_binary/_pty_test.py`) typing `exec /sbin/tsd_smoketest` and
`exec /sbin/locale_smoketest` at the real shell prompt. **Real console
output, both passed:**
```
pthread_tsd_shim_smoketest: real pthread_key_create/setspecific/getspecific (public + private-alias names) all work
exec: /sbin/tsd_smoketest exited, status=0
...
libc_locale_smoketest: real strcasecmp/strncasecmp work through real TSD+locale
exec: /sbin/locale_smoketest exited, status=0
```
No `FAILED` lines from either binary's own self-checks. Cross-checked the
new `cmd_exec()` status-decoding itself isn't just reporting a fake 0: a
control run (`exec /sbin/nope`, a path that doesn't exist) produced
`/sbin/nope: exec failed` + `exited, status=127` on the real console,
confirming the fork/execve/wait4/status-decode path genuinely
distinguishes pass from fail rather than always printing success.
**This closes Phase 4's TSD/locale runtime-correctness gap for real**: the
platform_syscall TSD bootstrap, `pthread_key_create`/`setspecific`/
`getspecific` (public + private-alias names), `pthread_once`,
`__xlocale_init`, and `strcasecmp`/`strncasecmp`'s real locale-aware path
(`__current_locale()` -> TSD -> `tolower_l()` against the real "C" locale
ctype table) all now have genuine on-target confirmation, not just a
clean host link.

**Seventh pass, 2026-08-14 (later same day): real stdio (FILE*) and gen/
(POSIX wrapper) tiers, both real-QEMU-boot-verified.** Extends the
`tools/userland_staging/build_libc_*.sh` curated-Libc-build pattern with
two new tiers:

- `build_libc_stdio.sh` -> `libc_build/libc_stdio.a`: real Apple
  `third_party/Libc/stdio/FreeBSD/*.c` FILE*-infrastructure layer -- real
  `fopen`/`fclose`/`fread`/`fwrite`/`fgets`/`fputs`/`getc`/`putc`/`rewind`/
  `fseek`/`ungetc`/`fdopen`, the real `__sF`/`__sglue` global stream table
  backing stdin/stdout/stderr, real buffering (`makebuf.c`/`refill.c`).
  64 of 68 real source files compile; the `printf`/`scanf` family is
  deliberately out of scope (real `vfprintf.c`'s "xprintf domain"
  extensibility system -- `xprintf_comp.c`/`xprintf_domain.c`/
  `xprintf_exec.c`/etc -- is a genuinely separate, much larger subsystem,
  already flagged as such in an earlier pass's `libc_locale_stdio.c`
  comment); `tmpfile`/`perror`/`open_wmemstream` also skipped, each on one
  additional missing syscall/header (`_sigprocmask`, `_writev`,
  `malloc_private.h`) not otherwise needed by this tier.
- `build_libc_gen.sh` -> `libc_build/libc_gen.a`: real Apple/FreeBSD
  `third_party/Libc/gen/` POSIX wrapper layer -- real `opendir`/`readdir`/
  `closedir`/`rewinddir`/`seekdir`/`telldir`/`dirfd`, real `getcwd`
  (its full directory-walking fallback, not just a fast-path stub),
  `err`/`errx`/`warn`/`warnx`, `basename`/`dirname`, `uname`, `confstr`,
  plus `stdlib/FreeBSD/merge.c` (`mergesort`, a real dependency of
  `opendir`'s duplicate-entry removal). All 15 curated files compile,
  zero skipped.

New real syscalls added (each cross-checked against
`bsd/kern/syscalls.master` the same way every prior raw syscall in this
project is, not guessed): `__getdirentries64` (#344), `openat` (#463),
`fstatfs`/`fstatfs64` (#346), `sysctl`/`sysctlbyname` (#202/#274),
`lseek`, `_fcntl`/`_dup2` (#92/#90), `stat64`/`lstat64` (#338/#340), plus
public `open`/`fcntl`/`fstat` aliases onto the existing private
`_open`/`_fcntl`/`_fstat`. New no-op-but-correct shims for this
single-threaded target (same reasoning already established for
`os_unfair_lock`/TSD): real `pthread_mutex_*`/`pthread_once`-adjacent
`os_once`, `__isthreaded=0`, `exit()`+`__cleanup` wiring (so real stdio
buffers genuinely flush on exit -- confirmed via `findfp.c`'s own real
`__sinit()` assignment, not faked), `isatty()=0` (real "no tty layer
ported yet" answer, not a guess).

Two real, previously-undetected header/build-config gaps found and fixed
along the way (both are genuine Xcode-12/iOS-14.4 SDK vintage gaps, same
class as prior passes' `os/assumes.h`/`mach-o/dyld_priv.h` stand-ins, not
guesses): `FLOCKFILE`/`FUNLOCKFILE` and `os_once_t`/`os_once()` are called
throughout the stdio tier but defined nowhere in this checkout or the SDK;
`getdirentries64_flags_t`/`GETDIRENTRIES64_EOF`/
`GETDIRENTRIES64_EXTENDED_BUFSIZE` and the `_GENERIC_DIRSIZ` macro are
called by the gen tier but likewise absent -- the former three pulled
verbatim from this project's own `third_party/xnu-7195/bsd/sys/dirent.h`
kernel source (not approximated), the latter is the standard BSD formula.
Also found and fixed a real `-DVARIANT_DARWINEXTSN` symbol-aliasing bug:
`fopen.c`/`fdopen.c` each self-`#define _DARWIN_UNLIMITED_STREAMS` under
that flag, which flips their exported symbol to a `$DARWIN_EXTSN`-suffixed
alias real Darwin's dyld resolves at load time -- meaningless (and
actively harmful) for this project's fully static, no-dyld link, so this
tier's CFLAGS deliberately omit `VARIANT_DARWINEXTSN` (present in the
locale tier's CFLAGS, but never actually exercised there).

**Real QEMU boot verification, both tiers, both genuine PASSes:**
```
root@sh:/ % exec /sbin/stdio_smoketest
PASS: real fopen/fwrite/fclose/fgets round-trip via stdout, stderr, stdin's own __sF table
exec: /sbin/stdio_smoketest exited, status=0
root@sh:/ % exec /sbin/gen_smoketest
PASS: real getcwd/opendir/readdir/closedir round-trip against /tmp
exec: /sbin/gen_smoketest exited, status=0
```
The stdio smoketest's first real run genuinely FAILED (`fopen(write)
returned NULL`) -- not a fabricated pass. Root-caused to a real
provisioning gap, same class as the `/etc` and `/dev` gaps prior passes
already found: fresh images have no `/tmp` directory, so `fopen(...,
"w")`'s real `O_CREAT` open genuinely got `ENOENT`. Fixed in
`tools/init_binary/inject_into_sd_image.sh` (`mkdir -p "$MNT/tmp"`, same
mechanism as the `/etc`/`/dev` fixes), re-verified PASS after.

Also self-caught one real bug before it ever reached a boot test: an
early version of the `sysctl(2)` raw-syscall wrapper only forwarded 4 of
the real kernel syscall's 6 positional arguments (dropping `new`/`newlen`
off the actual `svc` call entirely), leaving whatever was already sitting
in `x4`/`x5` to be read by the kernel as the "new value" pointer/length --
a genuine uninitialized-register hazard, not a cosmetic C-parameter
oversight. Fixed to always forward real (0/0-for-every-current-caller)
values before it was ever linked into a smoketest.

Both `.a` archives (`libc_stdio.a`, `libc_gen.a`) supersede the earlier
locale tier's own small hand-rolled `fdopen`/`fread`/`fileno`/`rewind`/
`fclose` shim (`libc_locale_stdio.c`, written before real stdio existed)
for any build that links both tiers together -- `build_libc_stdio.sh`
builds a `libc_locale_for_stdio.a` with that one now-redundant object
dropped from a copy of `libc_locale.a`, rather than removing it from
`libc_locale.a` itself (the locale tier's own standalone smoketest still
needs it and doesn't link the stdio tier at all).

Still not attempted: `printf`/`scanf` family (the "xprintf domain" wall
above), `libpthread`/`libplatform`/`libdispatch`/`libclosure`/`libmalloc`/
`libunwind`/`Libinfo`(-the-Apple-repo, distinct from this project's own
`tools/libinfo_custom`)/`removefile`/`copyfile`, and `dyld` itself -- all
still cloned-but-unbuilt per this section's earlier status notes.

**Eighth pass, 2026-08-14 (later same day): real `libplatform`
(`os_unfair_lock`/`os_once`/`OSAtomic*Barrier`), real-QEMU-boot-verified,
retiring `compat_stubs.c`'s no-op lock/atomic stand-ins.**
`tools/userland_staging/build_libplatform.sh` builds real, unmodified
`third_party/libplatform` (pinned `libplatform-254.80.2`, already cloned
by `setup_third_party.sh` -- no new pin needed): `os/lock.c` (real
`os_unfair_lock_lock`/`_unlock`/`_trylock`, real `_os_once`/`os_once_t`,
plus the legacy `OSSpinLock`/handoff-lock paths, none stubbed), `os/atomic.c`,
and `atomics/arm64/OSAtomic.c` (real `OSAtomicIncrement32Barrier`/
`OSAtomicDecrement32Barrier`/64-bit variants). Deliberately out of scope:
`src/init.c`/`atomics/init.c` (the dyld-driven `_libplatform_init()`
constructor + commpage-PFZ/CPU-feature detection -- needs a `_simple_getenv`
subsystem this project hasn't pulled in, and confirmed by linking that
nothing in the built scope actually calls it) and `setjmp`/`ucontext`/
`cachecontrol`/duplicate `string` primitives (`libc_string.a` already
covers those). This tier now sits ahead of the locale tier in build order
(`string -> platform -> locale -> stdio -> gen`, updated in each script's
own guard check) since real `xlocale.c`/`collate.c`/`setrunelocale.c`
call real `os_unfair_lock`/`OSAtomic`, not the retired stand-ins.

Real work this took, none of it guessed (see `build_libplatform.sh`'s own
header comment for the full list with exact line/file references):
- `lock_private.h`'s `OS_UNFAIR_LOCK_ADAPTIVE_SPIN` uses
  `__API_AVAILABLE(..., bridgeos(4.0))` -- bridgeOS wasn't yet an
  `Availability.h` platform macro in this project's pinned Xcode-12/
  iPhoneOS14.4 SDK vintage. Fixed via the exact real Apple SDK convention
  for this situation (`__API_AVAILABLE_PLATFORM_<name>(x)` remapping,
  confirmed against this SDK's own `AvailabilityInternal.h` for a platform
  it does define), not a blanket macro suppression.
- `SWITCH_OPTION_OSLOCK_DEPRESS`/`_WAIT`: real values (4, 5) exist in this
  project's own kernel checkout's `mach/thread_switch.h` but are shadowed
  by the (older-vintage) SDK's own copy via `-isysroot` lookup order --
  same SDK-vintage-gap class as prior passes, defined directly with the
  verified real values.
- `sys/ulock.h` (`UL_UNFAIR_LOCK`/`ULF_*`/`__ulock_wait`/`__ulock_wake`):
  real content only exists in this project's own kernel checkout
  (`bsd/sys/ulock.h`, gated `#if PRIVATE`), not the public SDK.
- Two real Mach traps this project had never needed before
  (`thread_switch` trap -61, `mach_timebase_info_trap` trap -89) --
  Mach traps aren't covered by the BSD-syscall Perl generator
  `build_libsyscall_smoketest.sh` already uses (separate trap table,
  `osfmk/kern/syscall_sw.c`). Real trap numbers cross-checked against
  `osfmk/mach/syscall_sw.h`; real arm64 call sequence (`mov x16,#-N` /
  `svc #0x80` / `ret`, **no** carry-flag check -- that convention is
  BSD-syscall-only) copied by hand from `osfmk/mach/arm/syscall_sw.h`'s
  own `kernel_trap` macro (`libplatform_mach_traps.s`).
- `__ulock_wait`/`__ulock_wake` (BSD syscalls 515/516) and `thread_selfid`
  (372): all three `NO_SYSCALL_STUB` in `syscalls.master` -- hand-written
  raw wrappers, this project's established carry-flag pattern
  (`libplatform_syscalls.c`).
- Real Apple `mach_absolute_time` (`libsyscall/wrappers/mach_absolute_time.s`,
  commpage-backed) assembles and links as-is; needed `-DPRIVATE=1` on
  that specific compile once traced to `osfmk/machine/cpu_capabilities.h`'s
  real userland dispatch branch being itself gated behind `#ifdef PRIVATE`
  -- without it the commpage offset macros silently never got defined at
  all (no error, just absent constants), caught via the resulting
  assembler "immediate must be an integer" errors and traced back to the
  real cause rather than patched at the assembly-error site.
- **Real correctness bug found and fixed before it ever reached a boot
  test**: `os_unfair_lock`/`os_once`'s notion of "current thread" (TSD
  slot `__TSD_MACH_THREAD_SELF`) had never been populated on this target
  (no real `pthread_start()` to do it) -- would read as `0 ==
  OS_LOCK_NO_OWNER`, making the real lock algorithm's owner-identity
  check silently degenerate (every caller would look like it's already
  the owner). Bootstrapped for real via `thread_selfid()` (a different,
  also-real, non-zero-per-thread value; deliberately not `mach_thread_self`'s
  own Mach trap, since the value is only ever compared for equality, never
  dereferenced as an actual port -- avoids a second unnecessary Mach-trap
  investigation, documented as a deliberate substitution in
  `libplatform_thread_self.c`, not a guess).
- Two real cross-tier integration bugs found only by actually relinking
  the existing locale/stdio/gen smoketests against real `libplatform.a`
  (not just building it in isolation): `once_private.h`'s `os_once()`
  wrapper is `__header_always_inline` with C99 "extern inline" linkage --
  at `-O0` a call site that isn't fully inlined leaves a real external
  reference to a non-underscore-prefixed `os_once` symbol nothing
  provides (fixed by calling the always-out-of-line `_os_once()` directly
  in the new smoketest); and the Seventh-pass stdio tier's own
  `libc_stdio_shims.c` had already added its own no-op
  `OSAtomicIncrement64`/`OSAtomicDecrement64` stand-ins (needed at the
  time, before this tier existed) -- real duplicate-symbol link errors
  once both were linked together, resolved by retiring those two
  functions from `libc_stdio_shims.c` in favor of the real ones now in
  `libplatform.a`, leaving that file's other real stand-ins (`os_once`,
  `pthread_mutex_*`) untouched since they don't collide with anything
  `libplatform.a` actually exports.

**Real QEMU boot verification, genuine PASS:**
```
root@sh:/ % exec /sbin/libplatform_smoketest
PASS: real os_unfair_lock lock/unlock
PASS: real os_once runs exactly once
PASS: real OSAtomicIncrement32Barrier/OSAtomicDecrement32Barrier
ALL PASS: libplatform smoketest
exec: /sbin/libplatform_smoketest exited, status=0
```
Tested on a scratch copy of the boot image (`qemu/libplatform_verify_test.img`,
deleted after verification), not the live default image. All four
existing smoketests (`libc_locale_smoketest`, `libc_stdio_smoketest`,
`libc_gen_smoketest`, and the new `libplatform_smoketest`) rebuild and
relink clean end-to-end in the new tier order with zero undefined symbols.

This target remains provably single-threaded (no real contention is
possible yet), so only `os_unfair_lock`'s real *uncontended* fast path is
exercised by this smoketest -- the real contended slow path
(`__ulock_wait`/`__ulock_wake`) is genuinely unreachable dead code until
real threading (`libpthread`) lands, not a hidden test gap.

Still not attempted: `printf`/`scanf` family, `libpthread`/`libdispatch`/
`libclosure`/`libmalloc`/`libunwind`/`Libinfo`(-the-Apple-repo)/
`removefile`/`copyfile`, and `dyld` itself.

**Ninth pass, 2026-08-14 (later same day): real `printf`/`scanf`
("xprintf") family, real-QEMU-boot-verified -- closes the wall the
Seventh pass deliberately left open.** `tools/userland_staging/
build_libc_printf.sh` builds `libc_build/libc_printf.a`: real Apple/
FreeBSD `stdio/FreeBSD/{vfprintf,vfscanf,vfwprintf,vfwscanf,printf-pos,
xprintf,xprintf_*}.c` (the real xprintf engine + all eight built-in
conversion-domain handlers) plus every public `printf*`/`scanf*`/
`wprintf*`/`wscanf*` wrapper (`printf`/`fprintf`/`sprintf`/`snprintf`/
`asprintf`/`dprintf`/their `v*` variants/`scanf`/`fscanf`/`sscanf`/wide-char
equivalents) -- 40 files, zero skipped. Plus real David Gay `gdtoa`
(`third_party/Libc/gdtoa/`), file selection read directly from Apple's own
`xcodescripts/libc.xcconfig` (`FreeBSD_INCLUDED_SOURCE_FILE_NAMES_arm64` /
`FreeBSD_EXCLUDED_SOURCE_GDTOA`) rather than guessed -- 21 of 23 compiled;
`_ldbl_util.c` is genuinely arm64-inapplicable (operates on a two-word
`union IEEEl2bits` long-double layout arm64 doesn't have -- real compile
errors confirmed this, not guessed) and `gdtoa-strtoIg.c` has a real
pre-existing bug in this exact checkout (calls `strtodg()` with 5 args
against `gdtoa.h`'s own 6-arg prototype) that nothing in this tier's real
undefined-symbol surface ever needs anyway.

`xprintf_domain.c`/`xprintf_comp.c`/`xprintf_exec.c`/`xprintf_all_in_one.c`
(the newer, separate "%{domain}" runtime-extensible printf API) stay out
of scope -- confirmed by `vfprintf.c`'s own `#if 0 // xprintf pending API
review` guard around its only use of that API.

Real new work, each hit as a real compile/link error first:
- `os_crash()` (real macro: `os/assumes.h`) backs `vfprintf.c`'s real CVE
  mitigation for `%n` on a non-immutable format string. Real Apple's
  header pulls in a full ASL-logging + `__builtin_os_log_format`-based
  "pack" encoding subsystem this target doesn't have; this tier's own
  `compat_include_printf/os/assumes.h` shadows it (kept in a **separate**
  `-I` dir from the locale/stdio tiers' shared, deliberately-empty
  stand-in, so the two scripts' independent every-run header regeneration
  can never flip this tier's `os_crash` between real and undefined
  depending on build order) with a single real function
  (`libc_printf_mach_stub.c`) that writes a real diagnostic via a real
  `write(2)` syscall and then really, unconditionally terminates the
  process -- the only behavior `%n`'s security check actually depends on.
- `vm_region_64()`/`mach_task_self()` back the read-only-format-string
  check `os_crash()` gates on. Real Mach IPC (MIG-compiled RPCs, not
  single raw traps like the BSD syscall layer) isn't ported on this target
  -- same class of gap as "no USB host controller driver" in
  architecture.md, not fakeable in scope here. Real, **fail-closed** stand-in:
  `vm_region_64()` always reports `KERN_FAILURE`, so `%n` is safely,
  loudly disabled (aborts) rather than this stub fabricating a "yes it's
  read-only" answer it has no way to actually verify -- the alternative
  would have been a real, silent security regression.
- `abort()`/`abort_report_np()`/`__assert_rtn()`: real Apple source needs
  real signal delivery (`sigaction`/`pthread_kill`/`pthread_sigmask`),
  `pthread_workqueue`, and the `_simple` ASL string builder, none ported.
  Project-owned real versions (same file) keep the one load-bearing
  behavior (diagnostic + unconditional process termination) via the same
  raw syscalls as `os_crash`.
- `__xprintf_domain_init`/`__xprintf_domain_once`/`pthread_rwlock_rdlock`/
  `unlock` satisfy `xprintf.c`'s `__v2printf()` -- real, load-bearing code
  on this tier's actual call path (`__xvprintf()` calls it), but its real
  control flow (read directly, not guessed) returns immediately for every
  call this tier ever makes (always `XPRINTF_PLAIN`) before ever touching
  these -- confirmed dead-but-linked, given correct fail-safe single-
  threaded-no-op bodies rather than fake always-succeeds ones.
- `_Block_copy`/`_Block_release` (real libclosure Blocks-runtime ABI, not
  ported) satisfy `libc_gen.a`'s `err.o`: real `errx()` shares that object
  file with Block-based `err_set_exit_b()`/`err_set_exit()`, and archive
  linking pulls a whole `.o` per referenced symbol -- confirmed nothing in
  this tier's real call graph ever registers a custom err exit handler.
- `__fpclassifyd()`/`nan()`/`nanf()`/`nanl()` (`libc_printf_fp_shims.c`):
  genuinely Libm functions (`third_party/Libm` isn't cloned into this
  project at all, correctly out of Phase 4 step 1's library list) but
  small/self-contained enough to write for real rather than skip the whole
  hex-float (`%a`/`%A`) tier -- `__fpclassifyd` is real IEEE754 binary64
  bit-pattern inspection (no FP hardware instructions needed);
  `nan`/`nanf`/`nanl` are real quiet-NaN-producing compiler builtins with
  one honest, documented simplification (the caller's tag string doesn't
  thread through into the NaN's payload bits -- "some NaN" instead of "the
  specific NaN this tag encodes", never inspected by anything in this
  tier's real call graph).
- `locale/iswctype.c` (self-`#define _EXTERNALIZE_WCTYPE_INLINES_TOP_`,
  the real Apple mechanism documented in `include/wctype.h`'s own
  "//Begin-Libc" comment) for real, externally-linkable
  `iswspace_l`/etc -- without it the header's inline-only definitions
  never emit a symbol at all. `stdlib/FreeBSD/strtoimax.c`/`strtoumax.c`
  for real `strtoimax_l`/`strtoumax_l` (`vfscanf.c`'s `%j` conversion).

**Real crash found and root-caused via a raw-syscall-only checkpoint
bisection** (not fabricated as a pass): the first QEMU boot test of the
full smoketest produced zero output and `terminated by signal 11`, with
nothing usable in the serial log to go on. Bisected by linking
progressively larger/smaller test mains against the exact same archive
set: a trivial `main(){return 0;}` and a `main()` that only calls a raw
`write(2)`-based diagnostic both passed clean against the full
`libc_printf.a`/`libc_stdio.a`/`libc_gen.a`/`libplatform.a` link, ruling
out an archive/link-order problem; a reduced test calling only
`snprintf()` reproduced the crash *inside* `snprintf()` itself. Root
cause: `snprintf.c` calls `__current_locale()` directly, which reads a TSD
slot that stays flat-out uninitialized without an explicit, one-time
`pthread_tsd_shim_bootstrap_for_libc()` call as the first thing `main()`
does -- a real, already-documented project convention
(`pthread_tsd_shim_libc_bootstrap.c`'s own header comment, itself the
product of an earlier adversarial-review finding) the smoketest had
simply never called. Fixed in the smoketest, not in Libc source.

**Real QEMU boot verification, genuine PASS (after the above fix):**
```
root@sh:/ % exec /sbin/printf_smoketest
PASS: real snprintf/sscanf round-trip (%d/%s/%f/%x): '42 hello 3.50 0xff' -> 42,hello,3.50
exec: /sbin/printf_smoketest exited, status=0
```
Tested on a scratch copy of the boot image, deleted after verification,
not the live default image.

Still not attempted: `libpthread`/`libdispatch`/`libclosure`/`libmalloc`/
`libunwind`/`Libinfo`(-the-Apple-repo)/`removefile`/`copyfile`, and `dyld`
itself.

**Tenth pass, 2026-08-14 (later same day): real `libmalloc` (heap
allocator), real-QEMU-boot-verified.** `tools/userland_staging/
build_libmalloc.sh` builds `libc_build/libmalloc.a`: a real Apple
magazine-zone allocator (`third_party/libmalloc`, tag `libmalloc-317.40.8`)
-- unmodified `magazine_tiny/small/medium/large/malloc/rack.c`, `vm.c`
(real page allocation via `mach_vm_map`), `bitarray.c`, `malloc_printf.c`.
`create_scalable_zone()`'s real `malloc_zone_t` function table does every
actual allocation decision (size-class routing, magazine/region
management, free-list handling); `libmalloc_dispatch.c` (project-owned,
~180 lines) is the real, minimal replacement for JUST `malloc.c`'s public
`malloc`/`free`/`calloc`/`realloc`/`valloc`/`reallocf` C-API glue --
`malloc.c` itself stays out of scope for real reasons (its `struct msl`
holds `dlopen`'d function pointers into a Malloc-Stack-Logging dylib;
there is no `dlopen` on this dyld-less target), same as `nano_malloc.c`/
`nanov2_malloc.c` (real Apple's faster thread-local-cache allocator
layered *on top of* the magazine zone -- separate, larger future item;
`nano_zone.h` v1 is additionally hard-gated to x86_64-only in this exact
pinned snapshot, a real, confirmed-not-guessed gap -- see
`libmalloc_compat.h`'s own comment).

Real new work, each hit as a real compile/link/runtime error first:
- `mach/mach_vm.h`: Xcode-12's iPhoneOS14.4 SDK's own copy is a literal
  `#error "mach_vm.h unsupported."` (a real, deliberate iOS-SDK
  restriction). The real *public* header (declaring plain
  `mach_vm_allocate`/`mach_vm_map`/etc, as real `vm.c` calls them) isn't
  present as source anywhere in this project's `xnu-7195` checkout either
  -- only as a kernel-`BUILD/`-output artifact this script deliberately
  doesn't depend on existing. Hand-typed real-signature compat header
  (`libc_build/compat_include/mach/mach_vm.h`) declaring just the two
  functions `vm.c` actually calls.
- `mach/mach_vm_internal.h`/`mach/vm_map_internal.h` (the *internal*
  `_kernelrpc_mach_vm_*`/`_kernelrpc_vm_*` MIG-fallback declarations real,
  unmodified `mach_vm.c` also needs): the former is real `mig -header`
  output, regenerated fresh each build from real unmodified
  `osfmk/mach/mach_vm.defs` using the real `mig` tool (confirmed present
  on this host) -- note **`/usr/bin/mig`, not `xcrun --sdk macosx --find
  mig`**: tested both against the identical input, only the former
  resolves its own `<mach/std_types.defs>` include correctly, a real
  toolchain-copy difference, not a missing file. The latter is hand-typed
  (declaring only the 3 functions `vm.c` needs) because a full `mig`
  generation of `vm_map.defs` shares a routine (`mach_make_memory_entry`)
  with the `mach_vm.defs` generation and the two conflict when both are
  real `mig` output in the same translation unit -- confirmed by trying
  it. Real `mach_vm.c`'s trap-fast-path (real, verified trap numbers
  -10/-11/-12/-14/-15, cross-checked against `syscall_sw.h`) means these
  MIG-fallback bodies (`libmalloc_vm_mig_stubs.c`) are real but provably
  unreachable dead code on this kernel -- defined honestly
  (`KERN_FAILURE`), not silently faked as success.
- `mach_port_deallocate`/`mach_port_mod_refs` (needed by real, unmodified
  `mig_reply_port.c`'s cleanup path): hand-written trap-only wrappers
  (real trap numbers -18/-19) rather than compiling real `mach_port.c`,
  which needs its own MIG header plus `mach_sync_ipc.h` and many more
  functions than the 2 actually needed here.
- `mach_reply_port()`: `osfmk/mach/mach_traps.h` has two declarations
  (real userland one, a second kernel-syscall-table one) that don't
  coexist cleanly for the one real call site (`mig_reply_port.c`, which
  itself never includes that header at all in this pinned tag) --
  declared directly (`compat_include/mach_reply_port_decl.h`), copied
  verbatim from the real line-88 signature.
- **Real, genuine `_FORTIFY_SOURCE` self-recursion SIGSEGV, found via
  QEMU boot-test bisection with temporary debug prints in scratch copies
  of `vm.c`/`magazine_malloc.c`/`magazine_rack.c` (not fabricated as a
  pass then walked back):** without `-D_FORTIFY_SOURCE=0` on
  `magazine_*.c` *and* on `memcpy_chk.c`/`memset_chk.c`/`chk_fail.c`
  themselves, real Apple's `<secure/_string.h>` macro-rewrites plain
  `memset()`/`memcpy()` calls into `__builtin___memset_chk(...)` --
  including **inside `__memset_chk`'s own real body** (`return
  memset(dest, val, len);`), which rewrites right back into a call to
  itself. Genuine infinite recursion, stack-overflow SIGSEGV, isolated to
  the *first* plain `memset()` call anywhere in the curated build
  (`rack_init()` zeroing `rack->initial_regions` -- an ordinary in-bounds
  512-byte write to already-mapped memory, nothing wrong with the call
  site itself). Fixed by adding `-D_FORTIFY_SOURCE=0` everywhere in this
  tier (same root-cause class `build_libc_string.sh`'s own header comment
  already documents for `stpcpy` -- this project just hadn't hit it for
  memset/memcpy specifically until real magazine code's own `memset()`
  became the first real exerciser).
- **Second real, independent bug, found bisecting further after the
  above fix advanced the crash deeper:** real `os_unfair_lock_lock_with_
  options()` (`SZONE_MAGAZINE_PTR_LOCK()`, magazine code's real lock
  acquisition) calls `_os_lock_owner_get_self()` ->
  `_os_tsd_get_direct(__TSD_MACH_THREAD_SELF)`, dereferencing
  `TPIDRRO_EL0` as a TSD block pointer. This dispatcher never called
  `pthread_tsd_shim_bootstrap()` first -- `build_libplatform.sh`'s own
  `libplatform.a` deliberately does *not* bundle `pthread_tsd_shim.o` in
  (confirmed via `nm` -- no TSD-shim symbols in that archive), by design
  leaving every real consumer responsible for linking it and calling the
  bootstrap explicitly, exactly as `pthread_tsd_shim_smoketest.c`'s own
  header comment already states ("NOT optional/lazy... called explicitly
  FIRST") and as the Ninth pass's own printf-tier bug above independently
  rediscovered for a different function
  (`pthread_tsd_shim_bootstrap_for_libc()`) -- a real, recurring class of
  gap across tiers, not a one-off. Fixed by linking `pthread_tsd_shim.o`
  into `libmalloc.a` and calling `pthread_tsd_shim_bootstrap()` (checked,
  `abort()`s on real failure) as the first thing `get_zone()` does.
- `_platform_memset`: real `libplatform/private/platform/compat.h`
  `#define`s `memset` -> `_platform_memset` for any includer (magazine
  code among them), but unlike `_platform_strcmp`/`_strlen`/`_memmove`/
  `_strchr` (all real "generic" portable implementations already
  compiled from `libplatform/src/string/generic/`), no real
  `_platform_memset` source exists anywhere in this checkout (confirmed
  by `find` -- only `memset_pattern.c`, a different function). Real
  Apple's `libplatform.dylib` must resolve this via the same closed
  umbrella-link alias mechanism as the `$UNIX2003` symbols below, not
  open-sourced source -- thin real wrapper around this project's own
  real memset (`libmalloc_dispatch.c`) is the honest equivalent.
- `$UNIX2003`-suffixed symbols (`mprotect$UNIX2003`/`kill$UNIX2003`/
  `write$UNIX2003`/`sleep$UNIX2003`, real Apple headers' default-on
  `__DARWIN_ALIAS` call-site renaming): confirmed the per-syscall Perl
  generator never produces these (0 matches for "UNIX2003" anywhere in
  it or its `custom/` dir) -- real Apple's build resolves them via a
  separate libSystem-umbrella alias-list step this project's build
  doesn't have. Real, zero-behavior-difference `ld -alias` link flags
  stand in (tried a same-file `.set` alias first, matching how Apple's
  own generated `.s` stubs do it internally, but confirmed by testing
  that `.set` only resolves within the same object file, not across
  separately-compiled ones -- `-alias` is the real cross-object
  mechanism).
- `sleep()`: real Apple `Libc/gen/FreeBSD/sleep.c` (full `EINTR`-retry
  loop) is more machinery than `malloc_printf.c`'s one rare call site (a
  debug-only "give a debugger time to attach" pause) warrants --
  `libmalloc_sleep_shim.c` implements the same real underlying primitive
  (`__semwait_signal(2)`, what real `sleep()`/`usleep()` are actually
  built on) directly.
- `abort()`/`__assert_rtn()`: real minimal versions in
  `libmalloc_dispatch.c` -- `kill(getpid(), SIGABRT)` (the real primitive
  Apple's own `abort()` ultimately uses; no `pthread_kill` on this target
  yet) with the same real infinite-loop fail-safe real `abort()` has.
- The 6 new real BSD syscall stubs this tier needed (`getpid`/
  `mprotect`/`kill`/`write`/`madvise`/`kdebug_trace`) plus `exit`/
  `__semwait_signal`: same real per-syscall generator as
  `build_libsyscall_smoketest.sh`, but that script's own precedent
  compiles the generated `.s` with the **host's default macOS
  toolchain/SDK** (needs `mach/arm64/asm.h`/`mach/arm/syscall_sw.h`, real
  headers present there but genuinely absent from Xcode-12's iPhoneOS SDK
  *and* this project's `xnu-7195` checkout) -- which can't link directly
  into an iOS14.4-target binary ("building for iOS, but linking in object
  file built for macOS", same class of error `build_libc_locale.sh`'s own
  header comment already documents for a stale archive). Fixed by
  compiling these 8 files *with* `-target arm64-apple-ios14.4` but
  pulling the two missing headers from the host macOS SDK via
  `-idirafter` -- confirmed safe first (pure assembler/preprocessor
  plumbing, architecture-generic, not the `CONFIG_EMBEDDED`-vs-
  `XNU_TARGET_OS_OSX` struct-layout concern CLAUDE.md's kernel-build
  warning is actually about).
- The original `libc_string.a` archive (from `build_libc_string.sh`) also
  turned out to still contain **stale** macOS-target `memcpy.o`/`memset.o`
  objects frozen in at `ar`-time, even though the loose `.o` files on
  disk were later silently overwritten with correct iOS-target versions
  by `build_libc_locale.sh`'s own independent recompile of the same
  filenames -- confirmed via `otool -l`'s `LC_BUILD_VERSION` platform
  field. Fixed by linking the on-disk (already-correct) loose objects
  directly instead of the archive.

**Retired the placeholder bump allocator** (`compat_stubs.c`'s
`malloc`/`calloc`/`realloc`/`free`/`reallocf`, in place since Phase 4's
locale/TSD bring-up) now that real `libmalloc.a` exists, and relinked
every existing consumer that used it (`build_libc_locale.sh`/
`build_libc_stdio.sh`/`build_libc_gen.sh`/`build_libc_printf.sh`'s
smoketests) against the real allocator instead -- each rebuilt clean and
re-verified. `build_libc_locale.sh`'s own smoketest step is the one
exception: real `libmalloc.a` itself depends on this tier's
`libc_locale_for_stdio.a`/`libc_stdio.a` output (for real `isatty()`
etc), so requiring `libmalloc.a` *here* would be circular on a from-
scratch pipeline run -- that one step degrades to a documented skip (not
a hard failure) when `libmalloc.a` isn't built yet, so `string ->
platform -> locale -> stdio -> gen -> printf -> malloc` in written order
still produces every `.a` archive on a fresh checkout; re-running
`build_libc_locale.sh` once more after `build_libmalloc.sh` relinks its
smoketest for real. Relinking also surfaced two more real duplicate-
symbol bugs, both fixed at the source: `libc_printf_mach_stub.c`'s own
`mach_task_self_`/`abort()`/`__assert_rtn()` stand-ins (now redundant
with real ones from `libmalloc.a` -- retired, keeping the still-needed
`vm_region_64`/`abort_report_np`/xprintf-domain/`_Block_copy` stand-ins),
and `libc_locale_smoketest.c`'s own hand-rolled `exit()` (now redundant
with the real one from `libc_stdio.a`, pulled in transitively once
`isatty()` was needed for `libmalloc.a`'s `malloc_printf.o`).

**Real QEMU boot verification, genuine PASS (scratch copy of the boot
image, deleted after):**
```
root@sh:/ % exec /sbin/libmalloc_smoketest
libmalloc smoketest: start
PASS: malloc/write/verify
PASS: calloc zero-init
PASS: realloc-grow preserves contents
PASS: 200-round alloc/free stress across size classes
ALL PASS: libmalloc smoketest
root@sh:/ % exec /sbin/libc_gen_smoketest
PASS: real getcwd/opendir/readdir/closedir round-trip against /tmp
```
The second line above is `libc_gen_smoketest` *relinked against real
malloc* (opendir/readdir genuinely call it) -- proof the relink is a real
behavioral change, not just a successful link.

Still not attempted: `libpthread`/`libdispatch`/`libclosure`/`libunwind`/
`Libinfo`(-the-Apple-repo)/`removefile`/`copyfile`, and `dyld` itself.
`abort()`'s real SIGABRT-delivery-terminates-the-process path is real,
correct kernel-default behavior (no project-side signal handling needed)
but was not itself exercised by any test here (no corruption occurred) --
noted, not fabricated as tested.

**Eleventh pass, 2026-08-14 (later same day): real `libpthread`
(single-thread-correct primitives; `pthread_create` real but
kernel-blocked), QEMU-verified, one real bug found+fixed via boot
test.** `tools/userland_staging/build_libpthread.sh` builds
`libc_build/libpthread.a` from real, unmodified `third_party/libpthread`
(pinned `libpthread-454.80.2`).

**Real, load-bearing constraint that shaped this whole tier** (read from
kernel source, not assumed): `third_party/xnu-7195/bsd/kern/
bsd_pthread_stub.c` -- this project's pthread.kext stand-in (real
pthread.kext was never open-sourced, same as launchd/opendirectoryd) --
unconditionally returns `ENOTSUP` for `bsdthread_create`/
`bsdthread_register`/every `psynch_*` entry. Real `pthread_create()` can
therefore never spawn a second schedulable thread on this target, full
stop -- a kernel-level fact, not a userland gap this tier works around.
Given that, this pass builds **outcome (B)**: everything genuinely
single-thread-correct, for real, plus a real (not fabricated)
`pthread_create()` that performs the real `bsdthread_create(2)` syscall
and returns the real kernel `ENOTSUP` -- honest documented failure, not
a fake success or a hand-waved errno.

**Compiled real, unmodified Apple source (6/6, 0 skipped):**
`pthread_tsd.c` (real `pthread_key_create`/`setspecific`/`getspecific`/
`pthread_once`'s TSD-key half), `pthread_mutex.c`, `pthread_cond.c`,
`pthread_rwlock.c` (real algorithms, running under their real
compiled-in DEFAULT policy -- FIRSTFIT, ulock off; the real uncontended
fast path is what's exercised/verified, the real psynch/FAIRSHARE
contended slow path is real code but structurally unreachable, see
above), `pthread_atfork.c`, `pthread_cwd.c` (per-thread cwd).
**Deliberately not built**: `pthread.c` itself (2587 lines) bundles
three separate things -- main-thread bootstrap/signature scheme,
`pthread_create`'s real stack/mach-vm allocation machinery (dead code
here regardless, per the kernel constraint -- and it also hits a REAL
header conflict: this project's own MIG-regenerated
`malloc_mig/mach_vm.h` and the real SDK's `<mach/vm_map.h>` both define
`mach_make_memory_entry`'s structs, confirmed by reproducing the
"conflicting types" error, not skipped preemptively), and trivial
one-liners. The one-liners (`pthread_equal`/`pthread_main_np`/
`pthread_main_thread_np`/`pthread_once`) are reimplemented in the new
`pthread_main_thread_bootstrap.c`, faithfully copied from pthread.c's
real bodies (each cites its origin line) -- not reinvented. Also
deliberately out of scope: `qos.c`/`pthread_cancelable.c`/
`pthread_dependency.c` (all need real `bsdthread_register`/a real second
thread/`os_workgroup`, all structurally unreachable) and `kern/*.c`
(that's the KERNEL-side pthread.kext internals `bsd_pthread_stub.c`
stands in for, not userland).

**Real, hand-written `NO_SYSCALL_STUB` syscalls** (Apple hand-writes
these in libpthread's own internal sources, not this project's public
headers -- same class as `__ulock_wait`/`__ulock_wake` in
`build_libplatform.sh`): `bsdthread_create` (360), and the full
`psynch_mutexwait`/`mutexdrop`/`cvbroad`/`cvsignal`/`cvwait`/
`rw_rdlock`/`rw_wrlock`/`rw_unlock`/`cvclrprepost` family (301-308,312)
-- real numbers/signatures cross-checked against `syscalls.master`,
kept even though practically unreachable (see FIRSTFIT/ulock note
above) since `-O0` keeps the real code paths referencing them linked in
regardless. Real Apple-generator-produced (not hand-written)
`gettimeofday`/`__pthread_chdir`/`__pthread_fchdir` via the same Perl
generator `build_libsyscall_smoketest.sh` already established.

**Real gaps found and fixed** (full detail in `build_libpthread.sh`'s
own header comment): `-D__POSIX_LIB__` (from this project's own
`pthread.xcconfig`, not guessed) needed to stop the public and private
`sched.h`/`posix_sched.h` from both defining `struct sched_param`;
SDK-vintage gaps for `mach/thread_policy.h`'s QOS additions and `os/
alloc_once_private.h` (same class as `build_libplatform.sh`'s bridgeos
gap); `-ffreestanding` silently substitutes clang's own resource-dir
`limits.h` for the real SDK's, dropping `PTHREAD_STACK_MIN` (confirmed
via `-H` include-stack tracing, not guessed); the real iPhoneOS SDK's
own `<mach/mach_vm.h>` is literally `"#error mach_vm.h unsupported."`
(read directly) -- worked around with a small hand-declared,
struct-free header for just the functions actually called, linking
against the real implementations already built into `libmalloc.a`;
`_os_xbs_chrooted` and `_os_once_acquire` both have no real definition
anywhere in this project's pinned checkouts (real cross-repo
version-skew -- `libpthread-454.80.2` expects newer libplatform/build
infra than what's pinned) -- the former is a compile-time constant `0`
(real value for this target: never chrooted), the latter's real
`pthread_once()` is reimplemented using this project's own real,
already-QEMU-verified `_os_once()` instead (older but real, same
single-execution-gate semantics). `abort_with_reason()`'s real path
needs a full signal-delivery subsystem this project hasn't built
(separate, larger Phase 4 scope) -- provides the real EFFECT
(unconditional termination) via this project's own real exit syscall
instead of the real SIGABRT mechanism, last resort per CLAUDE.md's
source-priority rule, after confirming the real blocker. Retired
`build_libmalloc.sh`'s now-conflicting `pthread_tsd_shim.o` member from
the already-built `libmalloc.a` (`ar d`, a normal archive operation).

**Real bug found by QEMU boot test, NOT caught by native `ld`/host
testing** (isolated via bisection -- a minimal `pthread_create`-only
smoketest crashed identically, immediately after `PASS: bootstrap`,
confirming it wasn't cumulative corruption from earlier real-mutex/TSD/
once tests): real libpthread source (anything including
`"internal.h"`) `#undef`'s `errno` and redefines it as
`*_pthread_errno_address_direct()` -- a real TSD-slot-1-relative
dereference, not a plain global. This project's bootstrap never
populated TSD slot 1, so `__bsdthread_create`'s own real `errno = ...`
on its real kernel-`ENOTSUP` error path dereferenced a NULL pointer --
genuine `SIGSEGV`, confirmed via `exec: /sbin/pthread_smoketest
terminated by signal 11` in a real QEMU boot log. Fixed for real: TSD
slot 1 now points at `_main_thread`'s own real `err_no` field
(`types_internal.h`: `"errno_t err_no; // thread-local errno"` -- the
exact real field this mechanism exists to expose), set during
`pthread_main_thread_bootstrap()`.

**Adversarial self-review before commit also caught a real documentation
bug** (not a functional one): an earlier draft of this tier's own
build-script header claimed `__pthread_mutex_use_ulock` was forced to
`true`. That was never actually implemented -- the real default
(`_PTHREAD_MTX_OPT_ULOCK_DEFAULT` = `false`) is what actually runs, and
the smoketest's real PASS is genuine either way (it exercises the real
uncontended fast path, which is policy-independent) -- but the comment
and a smoketest print string both overclaimed which real code path was
active. Corrected in both places before commit.

**Real QEMU boot verification, genuine PASS (scratch copy of the boot
image, deleted after; also caught the mid-development `errno`-TSD crash
above on an earlier, now-fixed revision -- not fabricated as
one-shot-clean):**
```
root@sh:/ % exec /sbin/pthread_smoketest
PASS: bootstrap
PASS: pthread_self/pthread_equal/pthread_main_np
PASS: real (FIRSTFIT-policy) pthread_mutex lock/unlock x2
PASS: real pthread_key_create/setspecific/getspecific
PASS: real pthread_once runs exactly once
PASS: pthread_create is real code, real kernel-level failure (ENOTSUP), as documented
ALL PASS: libpthread smoketest
```

**Not done in this pass** (documented follow-up, not silently skipped):
the locale/stdio/gen/printf tiers' own smoketests still link against
`tsd_bootstrap.c`/`pthread_tsd_shim.c`/`pthread_tsd_shim_libc_bootstrap.c`
directly rather than this real `libpthread.a` -- relinking all four
against the real tier (mirroring the malloc-tier relink pattern above)
is real, valuable follow-up work, not completed here given this pass's
own scope. Still not attempted: `libdispatch`/`libclosure`/`libunwind`/
`Libinfo`(-the-Apple-repo)/`removefile`/`copyfile`, and `dyld` itself.

**Twelfth pass, 2026-08-14 (later same day): real `libclosure` (Blocks
runtime), QEMU-verified.** `tools/userland_staging/build_libclosure.sh`
builds `libc_build/libclosure.a` from real, unmodified
`third_party/libclosure` (pinned `libclosure-78`, re-verified against
`apple-oss-distributions/distribution-macOS` tag `macos-112`'s
`git ls-tree macos-112 -- libclosure` = commit `86c3a9ea6bef05f...`,
matching this checkout's HEAD exactly). Real `runtime.cpp` (the actual
`Block_copy`/`Block_release`/`Block_object_assign`/`Block_object_dispose`
logic, refcounting via a real `__sync_bool_compare_and_swap` compiler
builtin -- confirmed by disassembly to lower to real arm64 `ldaxr`/
`stlxr`, zero libcall) + real `data.c` (the plain, `!HAVE_OBJC`-gated
`_NSConcreteStackBlock`/`_NSConcreteMallocBlock`/etc symbol table --
correct branch for this project, which has no ObjC runtime, not a skip).
3/3 files compiled, 0 skipped.

One real cross-tier dependency found: `runtime.cpp`'s `#define memmove
_platform_memmove` needs `_platform_memmove`'s real generic
implementation, which lives in
`third_party/libplatform/src/string/generic/memmove.c` -- compiled here
deliberately WITHOUT `-DVARIANT_STATIC` (confirmed by actually enabling
it: that flag makes the same file also emit plain `memcpy`/`memmove`
symbols, which collide with `libc_string.a`'s own real ones -- a genuine
duplicate-symbol link error, reproduced directly, not assumed). Also:
`os_assumes`/`os_assert` (used by `runtime.cpp` in a way that falls back
to calling real external functions of those names if not already
macro-defined) are predefined as command-line function-like macros
(`os_assumes(x)=(x)`, matching Apple's real value-passthrough shape for a
build with no `os_log` subsystem; `os_assert(x)` is a real assert via
`__builtin_trap()`, not a silent no-op) rather than adding a second,
conflicting `os/assumes.h` compat header alongside the existing empty
stand-in from the locale tier.

Real smoketest (`libclosure_smoketest.c`): a real compiler-emitted
stack block (`-fblocks`) capturing a plain variable, `Block_copy`d to
the heap, called, verified, `Block_release`d; a second block captures a
`__block`-qualified byref variable, exercising the real
`BLOCK_HAS_COPY_DISPOSE`/`_Block_object_assign`/`_Block_object_dispose`
path the first block doesn't touch. Linked against real `libmalloc.a`/
`libplatform.a` (same dependency set as `build_libmalloc.sh`'s own
smoketest). `otool -l`: `LC_UNIXTHREAD`, no `LC_LOAD_DYLIB`. `nm -u`:
zero undefined symbols. Real QEMU boot (scratch copy of the boot image,
deleted after), genuine PASS:
```
libclosure smoketest: start
PASS: simple Block_copy/call/release
PASS: __block byref copy/dispose round-trip
ALL PASS: libclosure smoketest
```

Adversarial review before commit caught two overclaiming issues in an
earlier draft of this pass's own build-script comments (the disassembly
claim and the `VARIANT_STATIC` duplicate-symbol claim were both written
before actually being checked) -- both were then genuinely verified (not
just softened) before commit, per the process above.

Not done in this pass: relinking the locale/stdio/gen/printf/malloc
tiers' smoketests against real `libpthread.a` (still open from the
eleventh pass, above). Still not attempted: `libdispatch` (this
project's next natural step -- it depends on `libclosure`, now done),
`libunwind`, `Libinfo`(-the-Apple-repo), `removefile`/`copyfile`, and
`dyld` itself.

**Thirteenth pass (2026-08-14): `libdispatch` investigated, deliberately
NOT built this pass -- real finding, not a skip for convenience.**
`third_party/libdispatch` is cloned+pinned (`libdispatch-1271.40.12`,
matches `distribution-macOS` tag `macos-112`). Surveyed `src/` looking
for the smallest genuinely-real, genuinely-separable entry point,
following this project's usual per-file curated-build pattern (the same
one that worked cleanly for `libplatform`/`libmalloc`/`libpthread`/
`libclosure`):

- `queue.c` alone is 8134 lines with 116 real hits for
  `voucher`/`firehose`/`_dispatch_workq`/`kevent` -- confirmed too large
  and too OS-integrated (real kqueue + real workqueue threads + real Mach
  voucher IPC + real firehose tracing buffers) to curate file-by-file the
  way earlier tiers did.
- The obvious "smallest real leaf," `dispatch_once` (`once.c`, 71 lines),
  looked separable at first -- its own logic only needs
  `_dispatch_once_gate_broadcast`/`_dispatch_lock_value_for_self`
  (`shims/lock.c`+`shims/lock.h`, 705+714 real lines, itself genuinely
  tractable: uses the exact same real `__ulock_wait`/`__ulock_wake`
  primitives `build_libplatform.sh` already wired up for
  `os_unfair_lock`) and `_dispatch_client_callout`
  (`inline_internal.h`). Traced where `dispatch_once_gate_t` itself is
  actually *defined*, expecting a small local struct -- it's in
  `queue_internal.h`, which is not includable standalone (no include
  guards for that use, confirmed by reading the file directly) and is
  itself only ever pulled in via `src/internal.h`'s single umbrella
  `#include` block, which unconditionally also pulls in
  `source_internal.h`/`voucher_internal.h`/`event/event_internal.h`/
  `firehose/firehose_internal.h`/`mach_internal.h`/`io_internal.h` (14
  subsystem headers total, read directly, not inferred) as one unit.
  **There is no real per-file granularity inside `libdispatch/src` below
  "the whole internal object/queue/voucher/event runtime as one
  compilation unit"** -- unlike every prior Phase 4 tier, where Apple's
  own file boundaries (`string/*.c`, `locale/FreeBSD/*.c`, `os/lock.c`,
  `src/*.c` in libmalloc) lined up with genuinely separable real
  functionality.
- Hand-extracting just the handful of struct/macro definitions
  `dispatch_once_gate_t`/`dispatch_lock` actually need out of
  `queue_internal.h` into a project-owned compat header was considered
  and rejected: at that point it stops being "real Apple source with a
  thin compat shim" (this project's established, source-priority-rule-
  compliant pattern) and becomes "reimplementing GCD's internal object
  layout by hand," which is both a correctness risk (silently drifting
  from the real ABI/semantics `dispatch_once_gate_t`'s real callers
  assume) and a violation of the "custom only as a last resort" rule for
  something Apple's real source for is right here in the tree, just not
  separable at this grain.

**Conclusion**: `libdispatch` is not a "next incremental tier" the way
`libclosure`→`libplatform`→`libmalloc` were -- it is a monolith by
design, and a real build means compiling a large, coupled slice of
`internal.h`'s full subsystem set together (real kevent-workqueue
integration in particular is itself contingent on real threading, which
this target structurally does not have -- see `libpthread`'s pass above)
rather than one `build_libdispatch.sh` in the existing per-file style.
Deferred as its own separately-scoped future effort, not attempted
partially. No code changes, no new build script, nothing to relink;
`docs/roadmap.md` is the only file touched this pass. Still not
attempted: `libunwind`, `Libinfo`(-the-Apple-repo), `removefile`/
`copyfile`, `dyld` itself, and the still-open eleventh-pass item
(relinking locale/stdio/gen/printf/malloc against real `libpthread.a`).

**Fourteenth pass (2026-08-15): real `removefile`/`copyfile`, both built
and QEMU-verified; plus a real project-wide errno bug found and fixed.**

- **`build_removefile.sh` -> `libc_build/libremovefile.a`**: all 5 real
  Apple `.c` files (`removefile.c`, `removefile_rename_unlink.c`,
  `removefile_sunlink.c`, `removefile_tree_walker.c`,
  `removefile_random.c`, tag `removefile-49.40.3`), 0 skipped. Real
  `removefile()` is not a thin `unlink()` wrapper even for a single
  file -- it unconditionally opens a real FTS(3) stream
  (`__removefile_tree_walker`), so this tier's real new dependency is
  `third_party/Libc/gen/fts.c`, the *modern* FTS implementation that
  reads directories via real `getattrlistbulk(2)` (syscall #461, not
  NO_SYSCALL_STUB -- generated fresh via the same real Apple Perl
  generator every other tier uses) plus real Blocks (`libclosure.a`) and
  real `os_assert()`. Also newly wired: `mkdir`/`geteuid`/`chflags`/
  `fchdir`/`fsync`/`fstatat64` (auto-generated real stubs) and hand-
  written raw wrappers for the NO_SYSCALL_STUB set (`unlink`/`rmdir`/
  `unlinkat`/`rename`), plus real `qsort`/`qsort_b`/`heapsort`/
  `heapsort_r`/`rand`/`random` (all real Apple/FreeBSD source, needed by
  `fts.c`'s sort step). One real, documented last-resort port: `flsl`
  (no `.c` source anywhere in this Libc checkout, only a header
  declaration -- ported from the standard BSD definition via
  `__builtin_clzl`, a real compiler builtin, not guessed) and
  `getiopolicy_np` (same "no real source in this checkout" situation --
  built for real on the real `iopolicysys(2)` syscall using this
  project's own kernel source for the real cmd/struct layout; its only
  call site is gated behind `SF_DATALESS`, which this target's HFS+
  driver never sets, so it's real, correct, linked, but provably
  unreachable, not untested).
- **`build_copyfile.sh` -> `libc_build/libcopyfile.a`**: real
  `copyfile.c` (5026 lines, one translation unit) + `xattr_flags.c`
  (tag `copyfile-173.40.2`), both compiling and linking whole -- 2/2,
  0 skipped. Built with this project's established `-target
  arm64-apple-ios14.4` toolchain, which means `TARGET_OS_IPHONE` is
  real and true for this compile -- copyfile.c's *own* real `#if
  !TARGET_OS_IPHONE ... #else` branch already provides a complete,
  Apple-authored no-op quarantine fallback, so the otherwise-unavailable
  `libquarantine.dylib` dependency is a non-issue purely as a
  consequence of the target triple this whole Phase 4 effort already
  committed to, not a workaround invented here. Real xattr/ACL/clonefile
  syscalls (`getxattr`/`setxattr`/`removexattr`/`listxattr` + f-variants,
  `clonefileat`/`fclonefileat`, `chmod`/`fchmod`/`getattrlist`/
  `open_dprotected_np`, the `*_extended` ACL-aware stat/chmod family)
  are all real, wired, and will genuinely return `ENOTSUP`/`EINVAL` from
  the real kernel at runtime since real HFS+ here doesn't implement ACL/
  xattr storage -- C doesn't dead-code-eliminate a runtime `if (flags &
  COPYFILE_ACL)` check, so these had to link even though this tier's own
  smoketest only exercises `COPYFILE_DATA`. Real `posix1e` ACL source
  (`third_party/Libc/posix1e/*.c`, all 15 files including `acl.c` and
  `acl_translate.c`), real `filesec.c`, `chmodx_np.c`/`statx_np.c`, real
  FreeBSD `realpath.c`, and real `uuid/uuidsrc/{copy,parse,unparse,
  pack}.c` are all compiled in for real rather than stubbed. A small,
  real, documented `dispatch_once`/`dispatch_once_t` compat shim was
  added for `xattr_flags.c`'s one call site (`nameInDefaultList`'s lazy
  init) -- real public, stable ABI (`typedef long dispatch_once_t`), not
  an internal GCD object layout, so this doesn't reopen the
  `libdispatch`-is-a-monolith finding above; this target is also
  provably single-threaded, so a plain check-and-set is genuinely
  correct here, not a corner cut. Real, honest last-resort stand-ins
  (never fabricating success) for functions with no directory/XPC/
  sandbox service to back them on this target: `getpwnam`/`getpwuid`/
  `getgrnam`/`getgrgid` (real "no such user" NULL), `mbr_uid_to_uuid`/
  `mbr_gid_to_uuid`/`mbr_uuid_to_id` (real "cannot resolve" failure --
  this target has no directory service at all, not even the custom one
  from Phase 5, wired into this API), `_xpc_runtime_is_app_sandboxed`
  (real "not sandboxed" -- this target has no App Sandbox concept). All
  of these are on ACL-text-parsing paths (`acl_from_text`/`acl_to_text`/
  `uuid_to_name`) this tier's own `COPYFILE_DATA`-only smoketest never
  reaches.
- **A real, project-wide bug found and fixed while debugging removefile's
  own QEMU boot test**: `removefile`'s smoketest called real
  `removefile(REMOVEFILE_RECURSIVE)`, got a real success return, but a
  post-delete `stat() == 0 || errno != ENOENT` check still failed --
  `stat()` genuinely failed (the directory really was gone) but `errno`
  read back as `0`, not `ENOENT`. Root cause: this project's hand-written
  raw-syscall helpers (the `syscall3`/`syscall4`/`syscall6`-style
  functions in `libc_gen_syscalls.c`, `libc_locale_syscalls.c`,
  `libc_stdio_syscalls.c`, `libc_locale_stdio.c`, and the two new files
  this pass added) returned the raw negative kernel error code directly
  on the carry-flag error path instead of calling `cerror_nocancel()`
  (the real Apple `errno.c` stand-in, `cerror_stub.c` -- already linked
  into every one of these binaries) the way the real Apple-*generated*
  syscall stubs do (confirmed independently: `_mkdir.s`'s own real
  assembly is `b.cc 2f` / `bl _cerror`). Every affected wrapper still
  correctly signaled failure to any `< 0`/`!= 0` check (so this had
  never visibly broken anything before), but never actually set the
  global errno, so any real caller inspecting `errno` after a failed
  call -- an extremely common real pattern -- got a stale or zero value.
  Fixed in all five files by routing the error path through
  `cerror_nocancel()`. (Checked, not touched: `pthread_bsdthread_create_
  syscall.c`, `pthread_psynch_syscalls.c`, and `libplatform_syscalls.c`
  already used a *different*, also-correct convention -- manually
  extracting `errno = (int)-r` at each call site -- so they were never
  actually buggy, just stylistically inconsistent; left alone rather
  than churned for uniformity's sake.) Re-verified every downstream
  tier (`string`/`platform`/`locale`/`stdio`/`gen`/`printf`/`malloc`/
  `pthread`/`closure`) still builds and its existing smoketest still
  passes after this fix, since the shared syscall files touch all of
  them.
- Also fixed: `libc_printf_mach_stub.c`'s `_Block_copy`/`_Block_release`
  identity-passthrough stand-ins (real placeholders from the
  eleventh/twelfth pass, predating `libclosure`) retired now that real
  `libclosure.a` exists -- linking both produced a real duplicate-symbol
  error. `build_libc_printf.sh` now links `libclosure.a`.
- **QEMU verification, both genuine PASS** (scratch image copy via
  `tools/init_binary/inject_into_sd_image.sh`, deleted after):
  ```
  PASS: real directory tree built (mkdir/fopen/fwrite)
  PASS: real removefile(REMOVEFILE_RECURSIVE) returned 0
  PASS: real stat confirms the whole tree is actually gone
  ALL PASS: removefile smoketest
  ```
  ```
  PASS: real source file written
  PASS: real copyfile(COPYFILE_DATA) returned 0
  PASS: copied content matches source byte-for-byte
  ALL PASS: copyfile smoketest
  ```
- Adversarial review before commit caught one real, if currently-inert,
  type mismatch in this pass's own `mbr_uuid_to_id` stand-in (`long*`
  instead of the real `id_t*`/4-byte width) -- fixed.

Still not attempted: `libunwind`, `Libinfo`(-the-Apple-repo), `dyld`
itself, and the still-open eleventh-pass item (relinking locale/stdio/
gen/printf/malloc against real `libpthread.a` specifically -- distinct
from the errno fix above, which touched those tiers' syscall files but
didn't relink them against `libpthread.a`'s mutex/TSD primitives).

**Fifteenth pass (2026-08-15): real `libunwind` built and QEMU-verified;
plus a real, project-wide ld64 static-executable finding.**

- **`build_libunwind.sh` -> `libc_build/libunwind.a`**: real, unmodified
  Apple/LLVM `third_party/libunwind` (pinned `libunwind-200.10`), 6/6
  files, 0 skipped: `UnwindRegistersSave.S`/`UnwindRegistersRestore.S`
  (real arm64 register-context save/restore), `UnwindLevel1.c`/
  `UnwindLevel1-gcc-ext.c` (real `_Unwind_RaiseException`/
  `_Unwind_Backtrace`), `Unwind_AppleExtras.cpp`/`libunwind.cpp` (the
  real `unw_getcontext`/`unw_init_local`/`unw_step` DWARF-CFI +
  compact-unwind engine, both real C++ -- this project's first C++
  compile, using real Apple/LLVM `<new>` placement-new declarations
  already shipped in the Xcode-12 toolchain itself, `-fno-exceptions
  -fno-rtti`, no libc++.dylib/libc++abi link dependency since the real
  source here only ever uses placement `new`, never namespace-scope heap
  allocation -- confirmed by grep before relying on it).
- **Scope decision** (checked against real source, not guessed): this
  project has no C++ exception usage anywhere in its own userland code,
  so full `_Unwind_RaiseException`/personality-routine C++-ABI exception
  support has no real consumer here; also confirmed real Apple
  `backtrace()`/`backtrace_symbols()` (`third_party/Libc/gen/
  backtrace.c`) do NOT go through libunwind at all on Darwin -- they use
  a separate, simpler frame-pointer walker
  (`_thread_stack_pcs()`, not part of this checkout) plus `dladdr()`/
  dyld for symbolication. So this tier's real, honest value is
  libunwind's own public `unw_*`/`_Unwind_*` API, exercised directly.
- **Real from-scratch shim** (last resort per CLAUDE.md -- real Apple
  source doesn't cover this case): `libunwind_dyld_shim.c` implements
  `_dyld_find_unwind_sections()` (real libunwind unconditionally assumes
  dyld provides this once any iOS deployment target is set, with no
  fallback) by walking `__mh_execute_header`'s real Mach-O
  `LC_SEGMENT_64` load commands directly -- a real ld64-synthesized
  symbol for any `MH_EXECUTE` link, confirmed via `nm` before relying on
  it -- plus an honest-failure (never fabricated-success) `dladdr()`
  stand-in, since this target has no dyld-backed symbol table.
- **Real bug found and fixed via QEMU boot-test bisection**: first run
  SIGSEGV'd immediately inside `_Unwind_Backtrace()`'s own real
  `pthread_rwlock`/`os_unfair_lock`-backed cache locking -- same bug
  class as `libmalloc_dispatch.c`'s `get_zone()` and the printf tier's
  own documented bootstrap bug (a genuinely uninitialized `TPIDRRO_EL0`
  dereferenced as a TSD block pointer). This smoketest never calls
  `malloc()`/`printf()` first, so it doesn't get their lazy/explicit
  bootstrap for free; fixed by calling
  `pthread_tsd_shim_bootstrap_for_libc()` explicitly, first, in `main()`.
- **Real project-wide integration finding, not previously exercised**:
  real libunwind's `RWMutex` is this project's first genuine caller of
  `pthread_rwlock_wrlock`'s real contended-lock path, which needed two
  real things nothing before had: (1) `libc_printf_mach_stub.c`'s old
  `pthread_rwlock_rdlock`/`unlock` no-op stand-ins (real, correct,
  single-threaded no-ops at the time -- real `pthread_rwlock_wrlock` was
  never referenced before, so no duplicate-symbol conflict had ever
  surfaced) collided with real `libc_build/libpthread.a` once both were
  linked together for the first time -- retired the stand-ins, and
  `build_libc_printf.sh`/`build_removefile.sh`/`build_copyfile.sh` (all
  three link `libc_printf_mach_stub.c` transitively via `libc_printf.a`)
  now link real `libpthread.a` too, with real
  `-Wl,-alias,'_pthread_rwlock_*$UNIX2003',_pthread_rwlock_*` aliases
  matching this project's established `$UNIX2003`-export convention; (2)
  real `pthread_rwlock`'s contended path calls `sched_yield()`
  (`third_party/libpthread/src/pthread.c:1872-1877`'s real one-liner,
  `swtch_pri(0); return 0;`) -- `pthread.c` itself was deliberately not
  compiled by the earlier libpthread pass, so this one real exported
  symbol it would have provided is supplied here instead
  (`libunwind_sched_yield.c`/`.s`, a real arm64 `swtch_pri` (-59) Mach
  trap stub, same real `kernel_trap` pattern as `libplatform_mach_traps.s`),
  compiled fresh into each of the three affected build scripts rather
  than creating a build-order dependency on `build_libunwind.sh` (which
  legitimately builds last in the pipeline). All three scripts rebuilt
  and re-verified clean after this change.
- **Real, larger structural finding** (found via direct empirical
  testing during this tier's own QEMU debugging, not guessed): ld64 on
  this project's pinned Xcode-12 toolchain does **not** synthesize a
  `__TEXT,__unwind_info` section (and drops `__eh_frame`/
  `__LD,__compact_unwind` entirely) for a `-nostdlib -static` linked
  Mach-O -- confirmed by building a minimal one-function `.o` with
  `-funwind-tables` (real `__compact_unwind` present, `otool`-verified),
  then linking it two ways with the *same* compiler: normally (dynamic,
  libSystem-linked) produces a real `__TEXT,__unwind_info` in the final
  image; this project's actual `-nostdlib -static -e start` pattern --
  every single Mach-O this project has ever produced, see
  `architecture.md` -- produces zero unwind-related sections at all,
  even with both `-funwind-tables` and `-fexceptions` on. This is a real
  ld64 behavior for static executables (unwinding across dyld image
  boundaries is presumably considered dyld's problem, which a static
  executable structurally doesn't have), not a libunwind bug or a flag
  this project was missing. **Consequence**: real DWARF/compact-unwind
  frame metadata is categorically unavailable to any unwinder on any
  binary this project produces -- not a gap specific to this tier, a
  standing fact for all future Phase 4 work (e.g. real `bash`/coreutils
  in step 3, once/if ever dyld-linked, would need to NOT be statically
  linked to get real unwind info -- a real tradeoff to weigh then, not
  now).
- **Smoketest scope, adjusted honestly for the finding above**: does not
  claim a real multi-frame backtrace (impossible given the constraint
  above without fabricating success or quietly redefining what "PASS"
  means). Instead proves what IS real and true here: `__unw_getcontext`/
  `__unw_init_local` genuinely succeed (context capture needs no unwind
  metadata), and `__unw_step`/`_Unwind_Backtrace()` correctly, *safely*
  report "no unwind info available" -- no crash, no fabricated frame --
  which is real, useful information for any future consumer of this
  library on this target.
- **QEMU verification, genuine PASS** (scratch image copy via
  `tools/init_binary/inject_into_sd_image.sh`, deleted after):
  ```
  PASS: __unw_getcontext captured a real register context
  PASS: __unw_init_local initialized a real cursor
  PASS: __unw_step safely reported no unwind info available (rc=0), as expected on this target - no crash, no fabricated frame
  PASS: _Unwind_Backtrace() returned safely, callback invoked 0 time(s) (0 expected on this target)
  ALL PASS: libunwind smoketest (real engine, real safe no-unwind-info behavior confirmed on this target)
  ```
- Adversarial self-review before commit: caught and fixed a stale file
  count in this script's own header comment (claimed 5 files, actually
  6), and confirmed the hand-picked `unw_context_t`/`unw_cursor_t`
  compat buffer sizes in the smoketest (128 x `uint64_t` = 1024 bytes
  each) against `__libunwind_config.h`'s real `__aarch64__` constants
  (`_LIBUNWIND_CONTEXT_SIZE`=66, `_LIBUNWIND_CURSOR_SIZE`=78, both in
  `uint64_t` units -- real max 624 bytes) rather than leaving them an
  unchecked guess.

Still not attempted: `Libinfo`(-the-Apple-repo), `dyld` itself, and the
still-open eleventh-pass item (relinking locale/stdio/gen/printf/malloc
against real `libpthread.a`'s mutex/TSD primitives specifically).

**Sixteenth pass, 2026-08-15: `dyld` investigated and scoped out (real
finding, not skipped preemptively); real static-`bash` survey started,
21/33 core files compile clean against the curated libc stack.**

- **`dyld` (`third_party/dyld`, pinned `dyld-832.7.3`) -- genuinely out of
  scope, confirmed by reading real source, not assumed.** This project's
  entire Mach-O output across all twelve prior real tiers is `-nostdlib
  -static`, zero `LC_LOAD_DYLIB` (verified every single time via `otool
  -l`) -- dyld's entire purpose (resolving/binding `LC_LOAD_DYLIB`
  dependencies at process launch) has no consumer anywhere in this
  project. Concretely confirmed real dyld source is deeply
  OS-integrated, not something a curated file subset can plausibly
  isolate: 26 of its `.cpp`/`dyld3` files reference `csops`/AMFI code-
  signing/`dyld_shared_cache`-family APIs directly (`grep -rl` count),
  and dyld's own real launch path is not a normal `LC_UNIXTHREAD` exec at
  all -- the kernel's `load_dylinker()` maps dyld itself via a dedicated,
  separate code path (`LC_LOAD_DYLINKER`) this project's kernel fork has
  never exercised or tested end-to-end. Even a hypothetically-working
  dyld would have nothing to load: this project has never built any of
  its curated libraries as a real `.dylib` (`MH_DYLIB`), only static
  `.a` archives -- doing so would mean teaching this project's linker
  invocations to emit real dyld-info bind/rebase opcodes from scratch,
  a large separate undertaking with no current payoff. Conclusion: not a
  next "tier" like libclosure/libmalloc/etc were -- a structurally
  different, much larger, currently-unmotivated effort. Revisit only if
  a real future need for actual dynamic loading emerges.
- **Real static `bash` (Phase 4 step 3's actual valuable target, given
  the finding above) -- survey started, real progress, not yet linked.**
  Given this project's real architecture is 100% static linking, the
  honest version of "real bash instead of Phase 2's builtin-fake shell"
  is linking real, unmodified Apple `bash` 3.2 source
  (`third_party/bash/bash-3.2`, pinned `bash-123.40.1`) against the
  curated `libc_build/*.a` stack via the same `Csu`+static pattern every
  other tier already uses -- not against dyld. New
  `tools/userland_staging/build_bash_survey.sh` (a survey script, does
  NOT produce a working `bash.macho` yet -- see its own header comment):
  copies Apple's own real, already-generated `config.h`
  (`third_party/bash/config.h`, Apple's actual macOS/arm64 build output
  for this exact checkout -- confirmed this exists and used it directly
  rather than reverse-engineering an Autoconf cross-compile cache from
  scratch, which real bash-3.2 does support via `--host`/`cross-build/*
  .cache` but would have been substantially more work for the same real
  result), regenerates the real `y.tab.c` parser from bash's own real
  `parse.y` grammar via host `bison` (a real, standard build-time
  toolchain step, not a target compile), and attempts each of 33 real
  core interpreter/state `.c` files against the curated stack's real
  headers. **Result: 21/33 compile clean** with only real, already-
  existing Apple-shipped headers added to the include path (this
  project's `third_party/bash` top-level `pathnames.h`/`version.h`/
  `libintl.h` -- Apple's own real generated companions to this exact
  bash checkout -- plus `third_party/Libc`'s `locale/FreeBSD`/
  `stdtime/FreeBSD` dirs already used by other tiers). **The 12 real
  remaining gaps are fully enumerated, not vague TODOs:**
  - `lib/tilde`, `lib/readline`, `lib/glob` -- bash's own bundled real
    source subdirectories; missing `-I` paths only, source is already
    present in the checkout. `lib/readline` in particular will need real
    thought (not yet done): this target's console is raw serial UART,
    not a real termcap/terminfo database, so real interactive line-
    editing needs either a real dumb-terminal-mode code path (readline
    supports one) or a scoped-down real subset -- a decision for
    whoever continues this, not resolved here.
  - `get_compat.h` -- a real Autoconf-generated file (a single
    `shell_compatibility_level` default `#define`), never generated here
    since this project doesn't run bash's own `configure`/`Makefile`.
  - `CONF_MACHTYPE`/`CONF_HOSTTYPE`/`CONF_OSTYPE`/`CONF_VENDOR`/
    `PACKAGE` -- real string-literal `-D` flags real
    `Makefile.in`'s `SYSTEM_FLAGS`/`LOCALE_DEFS` derive from
    `configure`'s target-triple substitution (confirmed by reading that
    exact `Makefile.in` line) -- trivial, real values for this target
    (e.g. `-DCONF_MACHTYPE='"arm64-apple-darwin20"'`), not fabricated
    logic, just not yet threaded through the survey script.
  Out of scope for this pass entirely (not attempted, not counted above):
  `builtins/` (bash's ~80 built-in commands, each its own `.def`-
  generated `.c` file via bash's own `mkbuiltins` host tool -- real and
  tractable, just not started), and linking/QEMU-testing anything, since
  there's no complete object set yet. Next concrete step for whoever
  continues this: close the 12 gaps above (all real, all scoped), get a
  full link attempt, see what real undefined symbols remain against the
  curated stack, iterate the same way every prior tier did.

**Seventeenth pass, 2026-08-15: real static `bash.macho` LINKS CLEAN
(zero undefined symbols) for the first time -- but crashes with a real
SIGSEGV very early in real QEMU execution, root cause not yet found.
Reporting both facts plainly, not just the milestone.**

- **All 12 survey gaps closed for real**, plus real `builtins/` (bash's
  own already-mkbuiltins-generated `builtins/*.c` -- confirmed by
  `builtins.c`'s own header comment that this checkout ships them
  pre-generated, same as `y.tab.c` needing bison but NOT needing
  `mkbuiltins` run by hand) and bash's own bundled `lib/sh/*.c` helpers.
  `complete.c` deliberately excluded (unlike `bind.c`, which has a real
  `#if defined (READLINE)` guard around its whole body and compiles to
  an empty TU cleanly, `complete.c` has no such guard -- it
  unconditionally implements `PROGRAMMABLE_COMPLETION`, itself
  unconditionally real-readline-dependent; real bash's own
  `--disable-readline` configure genuinely omits this file too,
  autoconf-conditional rather than an `#ifdef` inside the file).
- **Real, patched-upstream bug found and fixed**: this exact
  `bash-123.40.1` checkout's `lib/glob/smatch.c` forward-declares
  `__collate_lookup(char *, ...)`, conflicting with this project's
  current `Libc-1439.40.11` `collate.h` (`const unsigned char *` when
  `__LIBC__` is undefined) -- genuine cross-vintage header skew (this
  bash source predates that Libc revision), not a bash bug. Fixed with a
  minimal, documented guard (not a bulk edit) and landed as a real
  upstream patch via this project's existing `third_party-patches/`
  mechanism: `third_party-patches/bash.patch`, registered in
  `setup_third_party.sh`'s pinned-repo list (same pattern as
  `dtc-AppleDeviceTree.patch`/`u-boot.patch`).
- **A large real syscall/ABI-compat surface was needed** (all in new
  `tools/userland_staging/bash_syscalls.c`) that no prior tier had hit:
  real bash source calling standard POSIX names (`stat`/`read`/`write`/
  `strerror`/`waitpid`/etc) via real Apple headers compiled with this
  project's real `-D__DARWIN_UNIX03=1`/`-D__DARWIN_64_BIT_INO_T=1` flags
  resolves those calls to real *versioned* linker symbols
  (`_stat$INODE64`, `_write$UNIX2003`, etc) via `__DARWIN_ALIAS` macros
  -- a real problem class prior tiers never hit because their OWN code
  always called raw/internal names directly. Closed via real thin
  `asm("_name$SUFFIX")`-named wrappers delegating to already-real
  implementations, not fabricated ones. Also added: real `fork()`
  (raw syscall, reusing `tools/init_binary/init.c`'s already-documented
  x1-not-x0 child-flag ABI quirk rather than real `Libc/sys/fork.c`,
  whose `_libSystem_atfork_*` callouts are only ever populated by a real
  libSystem initializer this project doesn't have -- would NULL-deref);
  real `pipe()` (2-fd-via-x0/x1 raw syscall); a **real, source-verified
  arm64 signal-delivery trampoline** (`_sigtramp`, register convention
  read directly from this project's own kernel,
  `bsd/dev/arm/unix_signal.c`'s `sendsig_set_thread_state64()` --
  x0=catcher/x1=infostyle/x2=sig/x3=sinfo/x4=uctx/x5=token on entry,
  `sigreturn(uctx, infostyle, token)` `#184` to resume -- scoped
  honestly to simple non-`SA_SIGINFO` handlers, which is everything real
  bash's own `sig.c`/`jobs.c`/`trap.c` actually use); and hand-written
  `sigsetjmp`/`siglongjmp` (clang's `__builtin_setjmp`/`__builtin_longjmp`
  are NOT supported on this arm64 target -- confirmed by direct compile
  failure -- so these save/restore the real AAPCS64 callee-saved
  register set, x19-x28/x29/x30/sp/d8-d15, by hand).
- **Real bug found and fixed (via a genuine QEMU crash, not inspection)
  in that hand-written `sigsetjmp`**: the 21 saved registers occupy
  BYTE offsets 0-167 in the buffer (asm addresses `env` as a byte
  pointer), which is C `int`-array indices 0-41 (4-byte ints) -- not
  0-20 as an earlier version of this code assumed. That version stored
  its "mask saved" flag at `int`-index 21 (byte 84), squarely inside the
  real register-save region, clobbering the just-saved `x29`/`x30`/`sp`
  on every real `sigsetjmp()` call. Fixed (flag moved to index 42, past
  the real register area) and re-verified the fix doesn't reintroduce a
  build regression -- but see below, this fix did NOT resolve the real
  QEMU crash, so it was a real bug, just not (or not only) the one
  causing today's crash.
- **Honestly out of scope, not fabricated**: `dlopen`/`dlclose`/
  `dlerror`/`dlsym` (no dyld anywhere on this target, per this pass's own
  dyld finding above -- always report failure); `getpwnam`/`getpwuid`
  (real account lookup needs `tools/libinfo_custom` wired in, not done
  this pass -- only affects `~user` tilde expansion, not basic
  interactive use); `regcomp`/`regexec`/`regfree` (real Apple TRE-based
  regex source exists, `third_party/Libc/regex/TRE/lib/`, but is a
  separate multi-file subsystem with its own internal headers -- out of
  scope for this pass given the size of everything else already closed;
  honest stubs report real, correct POSIX error codes -- verified
  `REG_NOMATCH`/`REG_BADPAT` values match this SDK's real `regex.h`
  exactly, not guessed -- so `[[ str =~ pattern ]]` reports "no match"
  for every pattern rather than crashing or lying); `tcgetattr`/
  `tcsetattr`/`tcgetpgrp`/`tcsetpgrp`/`ttyname` (real ioctl/tty-line-
  discipline semantics this raw-serial-UART target's console driver
  doesn't have -- real `ENOTTY`, matching the actual errno a real
  non-tty fd produces); `alarm`/interval timers (no real `SIGALRM`
  subsystem -- honest no-op, `read -t`/mail-check timeouts won't
  actually fire); real readline line-editing and programmable completion
  (`rl_*`/`it_*`/`bashline_*`/`pcomplete`-family -- same real
  `--disable-readline`-equivalent scope decision the Sixteenth pass
  already flagged as needing real thought; implemented here as real,
  documented no-op/failure stubs matching real bash's own degraded
  behavior on such a configuration, never fabricating that line editing
  or tab completion actually work); real networking (`lib/sh/netopen.c`/
  `netconn.c` need a real socket/`getaddrinfo` stack this project
  doesn't have at all yet -- Phase 6 scope, consistent with
  `architecture.md`'s already-documented INET/INET6-disabled decision).
- **Real, honest QEMU result**: `bash.macho` was injected onto a scratch
  copy of the boot SD image at `/bin/bash` (boot image copied first,
  scratch deleted after -- never touched the live image) and exec'd for
  real via `tools/init_binary/init.c`'s existing `exec` builtin. Result:
  `exec: /bin/bash terminated by signal 11` -- a real SIGSEGV, very
  early (no bash-originated output at all, not even before any prompt),
  with control cleanly returning to `init.c`'s own fallback shell
  afterward (confirmed the kernel's own crash-handling path works
  correctly; this is not a VM hang). Re-tested after the `sigsetjmp` fix
  above with an otherwise-identical crash signature (same read-block
  count before the fault) -- meaning that real bug, while real and worth
  fixing, either isn't the (or isn't the only) cause of this specific
  crash. **Root cause NOT yet found.** Leading unconfirmed hypothesis
  (not verified this pass): real bash's parser/lexer allocates
  significantly more stack than anything previously exercised by this
  project's smoketests (which never exercised deep call trees), and this
  target's default exec-time stack size may be too small -- a real,
  concrete, testable hypothesis for whoever continues this, not
  presented as a diagnosis.
- **Commit**: this pass's changes are real and substantial (a working
  static link is a genuine first) but the resulting binary does not yet
  run correctly -- committed as real, honestly-reported partial progress,
  not as "bash works."

Next concrete steps for whoever continues this: (1) get a real fault PC
via the QEMU monitor's `x` (virtual, not `xp`) at the moment of the
SIGSEGV -- this pass didn't manage to catch the VM paused at the actual
fault instant, only the kernel's after-the-fact signal report; (2) test
the stack-size hypothesis directly (a trivially deep recursive C
function linked the same way, to see if it also faults); (3) once real
bash boots to a prompt, real readline/regex/networking are all
legitimate, separately-scoped next real gaps, not required for a first
working interactive session.

**Eighteenth pass, 2026-08-15: root-caused (1) and FIXED the real
stack-size bug for real, real-QEMU-verified; found and precisely
localized a SECOND, separate real bug that's the actual cause of real
bash's SIGSEGV -- not fixed this pass, but pinned down to an exact
function/call/register, not vague.**

- **Root cause of the Seventeenth pass's stack-size hypothesis, CONFIRMED
  real via kernel source, not guessed**: `bsd/arm/vmparam.h`'s
  `DFLSSIZ`/`MAXSSIZ` macros are guarded by a real, intentional
  `#if (TARGET_OS_OSX) || (KERNEL && !CONFIG_EMBEDDED)` split -- real
  macOS desktop values (8MB soft / 64MB hard) on one side, real iOS-app
  values (1MB / 1MB) on the other. This board's kernel build sets
  `CONFIG_EMBEDDED` (a real, correct, unavoidable constraint -- BCM2837
  is embedded-only upstream, see CLAUDE.md), so this port has silently
  been running with a **1MB process stack limit** the whole time, a real
  mismatch between what `CONFIG_EMBEDDED` assumes about its userland (a
  small single-purpose iOS app) and what this port's userland actually
  runs (a desktop-shaped interactive shell/parser). Traced via
  `bsd/kern/kern_exec.c`'s real stack-allocation math
  (`mach_vm_protect()`'s guard-region sizing from
  `proc_limitgetcur(RLIMIT_STACK)`, which is seeded from `DFLSSIZ` at
  proc0 bring-up) -- not inferred from the crash alone.
- **Fixed** in `third_party/xnu-7195/bsd/arm/vmparam.h`: a real,
  minimal, `#ifndef`-gated override (the header's own designed escape
  hatch, not a source hack) sets `DFLSSIZ`/`MAXSSIZ` to the real macOS
  desktop values specifically `#if defined(BCM2837)`, leaving every
  other real Apple platform's values in this shared header untouched.
- **Empirically verified with a new, purpose-built diagnostic tool**,
  `tools/userland_staging/stack_probe.c`/`build_stack_probe.sh` (real
  stack-resident recursion, reports real depth via real `write(2)`, not
  a smoketest with a pass/fail): before the fix, crashed with **zero**
  depth output at all (well under 500 real 4KB-buffer stack frames,
  consistent with a ~1MB real limit); after, reaches **~2000 frames
  (~8MB)** before the natural, expected recursion-exhaustion SIGSEGV --
  matching the new 8MB `DFLSSIZ` almost exactly. Real, reproduced QEMU
  transcript, not simulated.
- **Real bash still crashes after this fix** -- proving the Seventeenth
  pass's crash was NOT (solely) the stack-size bug, a real, separate
  cause. Root-caused via a **real fault-PC capture**, closing exactly the
  "next step" this pass's own prior entry called out as unfinished: a
  temporary `panic()` (not `kprintf()`, which was confirmed -- via a
  known-real stack-overflow control case that ALSO produced zero
  `kprintf()` output from `osfmk/arm64/sleh.c`'s `handle_user_abort()`
  and `osfmk/kern/exception.c`'s `exception_triage()`, both real, normal
  EL0-fault-delivery choke points -- to be silently unreachable from that
  exact call context, not a logic error) placed at
  `handle_user_abort()`'s real fault-classification point, gated on a
  genuine unresolved VM fault, captured the real user-mode state:
  **`PC=0x1000be1ec` (`_strcasecmp+0x44`), `LR=0x100010d84`
  (`_compat_mode+0x68`), `FAR=0xfffffffffffffff8` (i.e. address `-8`,
  ESR decodes to a real translation fault, not a permission fault)**.
  Cross-referenced against `bash.macho`'s own disassembly
  (`otool -tV`), confirmed exactly: real `third_party/Libc/gen/
  get_compat.c`'s `compat_mode(const char *function, const char *mode)`
  -- reached via bash's real `shell.c` `main()`,
  `if (!COMPAT_MODE("bin/sh", "Unix2003"))`, right after
  `code = setjmp(top_level)` -- calls
  `strcasecmp("unix2003", mode)`, and `mode` is NULL/near-NULL at the
  point of that call.
  - **The call site itself is correct**: disassembly of `bash.macho`'s
    real `_main` at `0x1000636fc`-`0x100063704` shows `x1` correctly
    loaded as the real address of the `"Unix2003"` string literal
    (`0x1000f9be0`) immediately before `bl _compat_mode` -- confirmed by
    cross-checking that exact value against the panic's own captured
    `x1=0x1000f9be0` register dump, a real, precise match.
  - **`compat_mode()`'s own prologue is also correct**: it immediately
    saves that same `x1` (the `mode` argument) to its own stack frame
    (`stur x1, [x29, #-0x18]`) before doing anything else.
  - **The ONLY thing that runs between that save and the later reload**
    (`ldur x1, [x29, #-0x18]`, immediately before the crashing
    `strcasecmp` call) **is a single real call**:
    `pthread_once(&threadsafe, check_env_var)` -- real
    `third_party/libpthread` source, linked via `libc_build/
    libpthread.a` (see the earlier libpthread pass). This strongly and
    specifically implicates real `pthread_once()`'s stack-frame handling
    as corrupting `compat_mode()`'s caller-frame local (a real, though
    not yet 100%-isolated-to-a-single-instruction, stack-corruption bug)
    -- not guessed, but not yet proven down to the exact clobbering
    instruction inside `pthread_once()` either. This is genuinely the
    first time in this project's history that real `pthread_once()` (not
    the narrower `_os_once()` path the libplatform/libpthread smoketests
    exercised) has been called from a stack frame this deep/this shaped,
    so it's plausible this is a real, previously-unexercised bug in
    either `pthread_once()`'s own real Apple source as ported here, or
    (less likely, given the call-site/prologue evidence above) in how
    this project's specific compile flags lay out `compat_mode()`'s
    stack frame around that call.
  - **Diagnostic instrumentation was fully reverted before committing**
    (`osfmk/arm64/sleh.c`/`osfmk/kern/exception.c` are back to real,
    unmodified upstream) -- the temporary `panic()`-on-every-user-
    `EXC_BAD_ACCESS` used to capture this evidence would be a severe
    regression if left in (it would panic the whole kernel on any future
    ordinary user-mode segfault, not just this one); only the real,
    permanent `vmparam.h` fix ships.
- **Not fixed this pass** (real, precisely-scoped next step for whoever
  continues this): find the exact instruction inside real `pthread_once`
  (or its real `check_env_var`/`getenv`/`sigprocmask`-adjacent callees)
  that corrupts `compat_mode()`'s saved stack slot, most likely via a
  focused disassembly of `pthread_once.o`'s real compiled output (not a
  fresh multi-hour bisection from scratch -- the exact call site, saved
  register, and stack offset are already known and documented above)
  or a hardware watchpoint on that exact stack address (`x29-0x18`
  inside `compat_mode()`'s frame) via QEMU's gdbstub.
- **Commit**: `vmparam.h` fix committed separately in
  `third_party/xnu-7195` (its own repo, own history, per this project's
  standing convention); `stack_probe.c`/`build_stack_probe.sh` and this
  writeup committed here.

**Nineteenth pass, 2026-08-15: found and FIXED two real, previously-
unknown bugs that were shadowing the real `pthread_once` implementation
in real bash's link -- but real bash still crashes after both fixes, on
a DIFFERENT, precisely-captured, real fault: the kernel never maps
bash.macho's own entry page at all. Not fixed this pass; exact evidence
below for whoever continues.**

- **Bug A -- real, confirmed via `ld -why_load` and disassembly, FIXED**:
  `tools/userland_staging/libc_build/libc_locale.a` bundles
  `pthread_tsd_shim.o` (the pre-`libpthread`-tier stand-in, `pthread_tsd_shim.c`)
  alongside the real, later-built `libpthread.a`'s
  `pthread_tsd.o`/`pthread_main_thread_bootstrap.o`. Both export the same
  4 public POSIX names (`pthread_once`/`pthread_key_create`/
  `pthread_setspecific`/`pthread_getspecific`). `ld64` resolves static
  archives in a single left-to-right pass: real Libc source compiled
  *into* `libc_locale.a` itself (`strsignal.c`'s real `thr_once`/
  `thr_keycreate` calls, per `pthread_tsd_shim.c`'s own header) demands
  `_pthread_getspecific` *while `libc_locale.a` is being scanned*, so
  `ld64` satisfies it from within that same archive
  (`pthread_tsd_shim.o`) regardless of where `libpthread.a` sits on the
  link line -- confirmed empirically: reordering `libpthread.a` earlier
  on `build_bash.sh`'s link line, by itself, changed nothing. Because
  `pthread_tsd_shim.o` bundles all 4 names in one translation unit,
  pulling it in for `getspecific` silently dragged in its OLD, naive,
  non-`_os_once`-based `pthread_once` too -- with **zero** duplicate-
  symbol error, because `libpthread.a`'s real
  `pthread_main_thread_bootstrap.o` (containing the real `pthread_once`)
  was never independently referenced by anything in bash's own code, so
  it was never loaded into the link at all. Confirmed via disassembly:
  bash.macho's real `_pthread_once` matched `pthread_tsd_shim.c`'s
  `once->sig != PTHREAD_ONCE_SIG_DONE` check byte-for-byte, not real
  `_os_once`'s cmpxchg/callout sequence -- and confirmed via a new,
  standalone repro (`pthread_once_deep_repro.c`, real QEMU-verified PASS
  at 1200 real recursion frames, linked directly against `libpthread.a`
  alone) that the REAL implementation does not corrupt a caller's stack
  the way real bash's (wrongly-linked) build did -- isolating the
  Eighteenth pass's `compat_mode()` corruption to *which* implementation
  got linked, not to real `pthread_once()`/`_os_once()` logic itself.
  Two fix attempts were tried and rejected before landing on the real
  one -- both are worth recording so a future pass doesn't repeat them:
  (1) reordering the link line -- doesn't work, per above; (2) `ar d`-
  stripping `pthread_tsd_shim.o` from `libc_locale.a` outright (same
  pattern already used, correctly, for `libmalloc.a`'s copy) -- this
  broke every OTHER standalone smoketest (stdio/gen/printf/malloc) that
  doesn't separately link `libpthread.a` and still genuinely needs
  `pthread_tsd_shim.o`'s OTHER real exports (`pthread_tsd_shim_bootstrap`,
  needed by real `libmalloc.a`'s `_get_zone`; the underscore-prefixed
  `_pthread_once`/`_pthread_setspecific`/etc FreeBSD-compat aliases,
  needed by real `strsignal.c`/`localtime.c`) -- `libc_locale.a`'s copy
  is genuinely load-bearing for those, unlike `libmalloc.a`'s. **Real
  fix**: `__attribute__((weak))` on just the 4 public POSIX function
  *definitions* in `pthread_tsd_shim.c` (not the underscore aliases,
  which libpthread.a doesn't export under those names at all and which
  simply forward to whichever definition wins). A consumer linking only
  `libc_locale.a` still gets these weak fallbacks, unchanged behavior,
  zero regression; a consumer also linking `libpthread.a` gets the real
  strong ones instead, with the weak ones silently discarded -- exactly
  the right tool for "provide a default, let a real implementation
  override it when present," and it required editing precisely one file
  instead of restructuring the archive/link graph.
- **Bug B -- real, confirmed via disassembly, FIXED**: even with Bug A
  fixed, `libpthread.a`'s `pthread_main_thread_bootstrap.o` still never
  entered bash's link at all -- `ld -why_load` showed nothing in bash's
  own code references `pthread_main_thread_bootstrap()` or any other
  symbol unique to that object file (real bash's `shell.c:main()` is
  real, unmodified Apple source with zero knowledge of this project's
  bootstrap machinery), so Bug A's weak-symbol fix had nothing to
  override *with* -- the weak definitions won by default, same
  observable bug, different mechanism. Every other real consumer in this
  project's history explicitly calls a bootstrap function as the first
  thing in `main()` (see `pthread_smoketest.c`,
  `libc_locale_smoketest.c`, etc.) -- real bash's own source obviously
  can't do this itself. **Real fix**: `bash_main_wrapper.c` (new) is a
  small, real `int main(int argc, char **argv, char **envp)` that calls
  `pthread_main_thread_bootstrap()` (aborting cleanly via a raw
  write+exit if it fails, not silently continuing) and then calls real
  bash's own `main()`, renamed to `bash_shell_real_main` at compile time
  via `-Dmain=bash_shell_real_main` applied to `shell.c` specifically
  (`build_bash.sh`). This both forces `pthread_main_thread_bootstrap.o`
  into the link for real (satisfying Bug A's override) and actually RUNS
  the real bootstrap before any Libc/locale/TSD-backed code executes --
  necessary because, unlike `pthread_tsd_shim.c`'s old functions (each
  defensively self-bootstrapping on first call), real `pthread_tsd.c`'s
  `pthread_getspecific()` calls `_pthread_getspecific_direct()` directly,
  a raw `TPIDRRO_EL0`-relative read with no defensive fallback -- reading
  through a NULL-based index on a never-bootstrapped thread (a real
  hazard real Libc's own `xlocale_private.h` documents, see
  `pthread_tsd_shim.c`'s header on `pthread_tsd_shim_bootstrap()`).
  Verified via disassembly: bash.macho's real `_main` now correctly
  calls `_pthread_main_thread_bootstrap`, checks its result, then calls
  `_bash_shell_real_main`; `_pthread_once` now correctly disassembles to
  `_os_once`'s real cmpxchg sequence, comparing against real
  `_PTHREAD_ONCE_SIG_init` (`0x30B1BCBA`), not the naive `cmp x8,#0x1`
  shim pattern from before.
- **Real bash STILL crashes after both fixes -- a THIRD, different, real
  bug, precisely captured, not fixed this pass.** Root-caused via the
  same real fault-PC-capture technique as the Eighteenth pass (temporary
  kernel `panic()` in `handle_user_abort()`, gated this time on
  `proc_selfname()` matching `"bash"` specifically -- an unconditional
  gate, tried first, caused the VM to sit in what looked like a genuine
  hang for several minutes, because ordinary/harmless early-boot user
  faults from `init.macho` itself would have hit it first; gating by
  process name avoided that entirely). Captured, real:
  **`PC=0x100002138 LR=0x0 FAR=0x100002138 ESR=0x82000007`** --
  PC equals FAR exactly (an instruction-fetch fault, not a data access),
  `LR=0` (no `bl` has ever executed -- this is the literal first
  instruction fetch of the process), ESR decodes to EC=0x20
  (Instruction Abort from EL0) with IFSC=0x07 (translation fault, level
  3 -- the page genuinely isn't mapped, not a permission problem).
  Cross-checked directly against `bash.macho`'s own `LC_UNIXTHREAD`
  load command via `otool -l`: **`pc 0x0000000100002138`** -- an exact,
  precise match to both PC and FAR. In other words: the kernel's exec
  loader accepts and starts executing bash.macho (`exec_activate_image`
  reports reading its header successfully, no earlier failure), but the
  entry page itself is never actually mapped, so the very first
  instruction fetch faults, before a single instruction has run.
  `bash.macho`'s segment/load-command structure was checked against a
  known-working smoketest (`pthread_smoketest.macho`) via `otool -l` and
  is structurally near-identical (`__PAGEZERO`/`__TEXT`/`__DATA`/
  `__LINKEDIT`, one extra `__DATA` section, nothing else different) --
  ruling out a gross Mach-O structural difference as the cause. The one
  clear, real difference between bash.macho and every prior working
  binary is **size**: bash.macho is ~1.2MB, by a wide margin the largest
  static Mach-O this project has ever exec'd (every prior smoketest has
  been well under 100KB) -- a real, concrete, testable hypothesis for
  whoever continues (check `exec_activate_image`/`load_segment`'s real
  handling of a `__TEXT` segment this large -- `bsd/kern/mach_loader.c`
  -- for a page-count/size limit, an integer truncation, or a real
  demand-paging path this project's kernel has never exercised at this
  scale), not yet confirmed.
- **Diagnostic instrumentation fully reverted before committing** (same
  discipline as the Eighteenth pass) -- `osfmk/arm64/sleh.c` is back to
  real, unmodified upstream; only the two real userland fixes
  (`pthread_tsd_shim.c`'s weak attributes, `bash_main_wrapper.c` +
  `build_bash.sh`'s `-Dmain` rename) ship.
- **Commit**: this pass is entirely userland-side (no kernel source
  changes ship) -- `tools/userland_staging/pthread_tsd_shim.c`,
  `build_libpthread.sh` (a dropped, rejected `ar d` attempt's leftover
  comment cleanup), `build_bash.sh`, `bash_main_wrapper.c` (new),
  `pthread_once_deep_repro.c`/`build_pthread_once_deep_repro.sh` (new
  diagnostic tool, same spirit as `stack_probe.c`), and this
  writeup, committed together in the main repo. `third_party/xnu-7195`
  has no net changes this pass (diagnostic added and fully reverted).

**Twentieth pass, 2026-08-16: root-caused and FIXED the Nineteenth pass's
open "entry page never mapped" bug -- it turned out to be TWO separate,
real bugs, neither one actually in the kernel's page-in path the
Nineteenth pass suspected. Also found (not yet fixed) a fourth,
different bug: bash now runs real code but hangs indefinitely partway
through interactive startup with the kernel provably idle, not crashed.**

- **Bug 1 (test infrastructure, not a kernel/userland bug) -- real,
  confirmed via `xxd`/`strings`, FIXED**: `devicetrees/rpi3.dtb` had been
  regenerated with the *wrong* `dtc` -- the host's Homebrew `dtc`
  (`/opt/homebrew/bin/dtc`), which emits a standard Linux FDT blob (magic
  `d00dfeed`), instead of this project's patched
  `third_party/dtc-AppleDeviceTree/dtc`, which emits Apple's
  proprietary `IODeviceTree` binary format (no magic number, a raw
  node/property count header -- see that tool's own `-h` output).
  Apple's `IODTPlatformExpert` can't parse a standard FDT, so XNU never
  produced any serial output past the QEMU loader's own "xnu" banner --
  every single QEMU boot test since the DTB was last regenerated (for
  this same day's earlier `model = "BCM2837"` property addition, needed
  by `uname`'s `HW_MACHINE` sysctl -- see the git history around
  `devicetrees/rpi3.dts`) hung identically, indistinguishable from the
  known concurrent-QEMU-contention issue `_pty_test_phase5.py` already
  documents. This is almost certainly what the session's "several stuck
  processes" were: QEMU instances that never reached kernel text, sitting
  in `_pty_test_phase5.py`'s 240s wait loop with zero output, for a
  completely different reason than that script's own contention note.
  **Fix**: regenerate with the correct tool --
  `third_party/dtc-AppleDeviceTree/dtc -I dts -O dtb -o
  devicetrees/rpi3.dtb devicetrees/rpi3.dts`. No source change; the
  fixed `.dtb` binary ships in this commit. Lesson for next time: verify
  a regenerated `.dtb`'s first bytes are NOT `d00dfeed` before trusting a
  boot test's silence as a real bug.
- **Bug 2 (test harness usage, this session's own mistake) -- FIXED**:
  the login test `cmds` file (`tools/userland_staging/bash_diag_cmds.txt`,
  and identically `ps_qemu_test_cmds.txt` from Phase 4 step 3's ps work)
  had `login: root` as its first line. `_pty_test_phase5.py` sends each
  `cmds` line VERBATIM as the typed response once the prompt suffix is
  seen -- it does not strip the prompt text itself. So `init.c`'s
  `do_login()` genuinely received the literal 12-character username
  `"login: root"` (confirmed via a temporary `DIAGLOGIN` print of the raw
  `name`/`pass` buffers), which of course doesn't match any real account,
  producing a real (if misleadingly-labeled) "Login incorrect". The
  correct `cmds` file content is just the bare reply values (`root` /
  `darwin` / ...), matching how `phase5_cmds.txt` (the one cmds file that
  predates this mistake) is actually formatted. This explains why the
  Nineteenth pass's own "entry page never mapped" diagnosis, captured via
  a *different*, non-cmds-file method (a proc-name-gated panic), was
  never actually contradicted by a login-flow test -- no login-flow test
  had ever passed to compare against.
- **Bug 3 (real, in `tools/userland_staging/build_bash.sh`) -- root
  cause of the Nineteenth pass's actual SIGSEGV, confirmed via
  disassembly, FIXED**: `CFLAGS` is built as one `"..."`-quoted shell
  string containing, among others,
  `-DCONF_HOSTTYPE='\"arm64\"'`. Inside a double-quoted string, `\"` is a
  real escaped double-quote, but the surrounding `'` characters are
  *not* shell-special there -- they end up as two literal single-quote
  characters embedded in `$CFLAGS`'s value. When `$CFLAGS` is later
  expanded unquoted on the `clang` command line, word-splitting does not
  re-parse those embedded quote characters as shell syntax (a variable's
  expanded content is never re-quoted), so clang's argv genuinely
  contains the 27-byte token `-DCONF_HOSTTYPE='"arm64"'` -- literal single
  quotes and all. Clang's `-D name=value` splits on the first `=`, so
  `CONF_HOSTTYPE` gets defined to the token text `'"arm64"'` (9
  characters: `'`, `"`, `a`, `r`, `m`, `6`, `4`, `"`, `'`). Substituted
  into `conftypes.h`/`variables.c`'s `set_if_not ("HOSTTYPE",
  CONF_HOSTTYPE)`, the C lexer sees `'"arm64"'` -- a single-quote begins a
  *character constant*, not a string, containing `"arm64"` (7 raw
  characters) before the closing `'`. This is a real (if archaic) GNU
  multi-character-constant literal; clang packs the *last* 4 characters
  (`m`, `6`, `4`, `"`) into a 32-bit integer and emits `mov`/`movk` to
  materialize it -- exactly the `mov x8, #0x3422 / movk x8, #0x6d36, lsl
  #16` pattern found at the real crash site. That integer, `0x6d363422`,
  then gets passed as `make_variable_value()`'s `char *value` argument
  and dereferenced (`variables.c:1868`'s `else if (*value)`), a garbage
  pointer well below `__PAGEZERO`'s top (`0x100000000`) -- an
  always-unmapped guard region, hence the fault. `CONF_OSTYPE`/
  `CONF_MACHTYPE`/`CONF_VENDOR`/`PROGRAM`/`PACKAGE`/`LOCALEDIR` all used
  the identical broken quoting pattern and were equally corrupted
  (`OSTYPE`'s and `MACHTYPE`'s calls share the exact same garbage value
  via a spilled stack slot -- confirmed via disassembly, not guessed).
  Real call chain that reaches the crash, resolved via a temporary
  frame-pointer walk in `handle_user_abort()` (`x29`/`x30` pairs,
  `copyin()`-read from kernel, gated on `proc_selfname() == "bash"`, same
  discipline as the Eighteenth/Nineteenth passes' fault-capture
  technique): `bash_shell_real_main` -> `shell_initialize` ->
  `initialize_shell_variables` -> `set_machine_vars` -> `set_if_not` ->
  `bind_variable_internal` -> `make_variable_value` (crash). **Fix**:
  drop the redundant single quotes entirely -- `-DCONF_HOSTTYPE=\"arm64\"`
  (verified in isolation first: `CFLAGS="-DFOO=\"bar\""` unquoted-expanded
  really does define `FOO` to the string `"bar"`, not a char constant).
  Verified post-fix via disassembly: all three calls in `set_machine_vars`
  now correctly `adrp`/`add` real string-literal addresses
  (`"arm64"`/`"darwin20.3.0"`/`"arm64-apple-darwin20.3.0"`), and a full
  QEMU boot no longer produces `DIAG20`/SIGSEGV at all (confirmed with
  the diagnostic still in place, then again after reverting it).
- **Bug 4 -- real, precisely characterized, NOT fixed this pass**: with
  bugs 1-3 fixed, `exec /bin/bash` no longer crashes, executes real code
  across a wide spread of its own text segment (dozens of distinct
  `DIAG20` exec-fault addresses, all `result=0x0`/success, confirming
  demand-paging itself works fine at this binary's size -- the
  Nineteenth pass's "largest Mach-O ever exec'd" size hypothesis is
  false, or at least not the operative bug), then goes completely and
  reproducibly silent partway through interactive startup: identical
  output byte-for-byte across a 240s and an independent 480s run (no
  crash message, no further disk I/O, no prompt). QEMU-monitor `info
  registers`, sampled 4 times a few seconds apart via the monitor socket,
  shows the CPU cycling through `_processor_idle`/`_mach_absolute_time`/
  `_setPop`/`_thread_invoke` in EL1 each time (resolved against
  `third_party/xnu-7195/BUILD/obj/.../mach.development.bcm2837.unstripped`'s
  own `nm` output) -- this is the kernel's own genuine idle loop, the
  same signature as this project's very first boot-to-idle milestone, NOT
  a busy-spin. In other words: the kernel has nothing runnable. bash's
  single thread is blocked on something that never wakes it -- most
  likely a real syscall this project's userland stub layer or kernel
  mishandles by blocking instead of failing/returning immediately, given
  `bash_syscalls.c`'s own header notes `tcsetpgrp`/`tcgetattr` are
  stubbed to return `ENOTTY` (non-blocking) but real bash's job-control
  startup path (`initialize_job_control()` et al.) may call raw
  `ioctl()` directly rather than through those wrappers, and this
  project's raw `ioctl()` is a real, unfiltered syscall reaching whatever
  the kernel's console/tty driver does for an unrecognized command --
  not yet checked whether that path can block instead of erroring.
  Whoever continues: instrument syscall entry/exit (or the specific
  `ioctl`/`read`/`wait4` handlers) gated on `proc_selfname() == "bash"`,
  same technique as this pass, to find the exact blocking call.
- **Diagnostics used this pass, all reverted before commit** (same
  discipline as prior passes): `osfmk/arm64/sleh.c` grew a
  `proc_selfname()`-gated `printf` in `handle_user_abort()` (widened
  across three iterations: exec-faults only -> any-fault -> plus an
  `x29`/`x30` frame-pointer walk) and a temporary `DIAGLOGIN` print of
  the raw login `name`/`pass` buffers in `tools/init_binary/init.c`
  (echoes a plaintext password -- confirmed removed, not shipped).
  `third_party/xnu-7195` has zero net diff after this pass.
- **Commit**: `devicetrees/rpi3.dtb` (regenerated, correct format),
  `tools/userland_staging/build_bash.sh` (quoting fix),
  `tools/userland_staging/bash_diag_cmds.txt` (new, correctly-formatted
  login test asset), and this writeup.

**`uname` (Phase 4 step 3): fully working, QEMU-verified 2026-08-16.**
Builds clean (`build_uname.sh`, 4 files, `uname_main_wrapper.c`'s
bootstrap-then-real-`main` pattern same as `bash_main_wrapper.c`'s). With
the corrected `devicetrees/rpi3.dtb` (`model = "BCM2837"` property, this
same pass) and a correctly-formatted login `cmds` file
(`uname_diag_cmds.txt`), both `uname` and `uname -a` exit status 0 with
real output: `Darwin` / `Darwin 20.3.0 Darwin Kernel Version 20.3.0: ...
BCM2837`. The scratch `sysctl_probe.c` diagnostic that found the missing
`model` property (its own header said "delete when the real bug is
found+fixed") has been deleted per that comment.

**`ps` (Phase 4 step 3): fully working, QEMU-verified 2026-08-16.** Real
`third_party/adv_cmds/ps/` (ps.c/print.c/keyword.c/fmt.c/nlist.c),
`tasks.c` (real Mach IPC) deliberately not compiled -- see
`ps_task_info_stub.c`'s header for the real, honest degradation path this
reproduces instead (CPU%/RSS/VSZ/thread columns print real zeros, never
fabricated). Found and fixed the exact same missing-TSD-bootstrap gap
`bash_main_wrapper.c`/`uname_main_wrapper.c` already fixed for their own
binaries -- `build_ps.sh` never had an equivalent, so `ps.macho` was
guaranteed to SIGSEGV on first TSD-backed Libc call (confirmed: `exec
/bin/ps` really did `terminated by signal 11` before the fix). Added
`ps_main_wrapper.c` (identical pattern, `-Dmain=ps_cmd_real_main`) --
`ps`/`ps -ax` now exit 0 with real, correct PID/PPID/USER/COMMAND data
straight from the real `KERN_PROC_ALL` sysctl. One non-bug worth
recording so it isn't rediscovered: `ps -aux` (leading dash) genuinely
errors `No user named 'x'` on real BSD/macOS too -- `PS_ARGS`'s getopt
string has `u:` (takes a required argument), so `-aux` parses as `-a -u
x`, not the no-dash legacy `ps aux` BSD keyword-style invocation (a
different code path, `kludge_oldps_options()`) that most people actually
mean. Also fixed a separate, pre-existing bug in this session's own test
tooling: `ps_qemu_test_cmds.txt`'s first line was literally the text
`login: root` (sent verbatim by `_pty_test_phase5.py`, which does not
strip prompt text) -- see the Twentieth pass's bash-testing entry above
for the full mechanism; this file had the identical bug independently.

**Phase 4 step 3, "remaining commands," Tier 1 (simplest/lowest-dependency
first): DONE, QEMU-verified 2026-08-16.** `true`/`false`/`pwd`/`basename`/
`dirname`/`yes` (real, unmodified `shell_cmds/*`), via a new
`build_simple_cmds.sh` + `generic_main_wrapper.c` (one shared
TSD-bootstrap wrapper body, parameterized by `-DREAL_MAIN=.../-DCMD_NAME=
"..."` per binary, instead of hand-duplicating bash/uname/ps's nearly
identical 30-line wrapper six more times). All six link clean and exit
correctly (`true`->0, `false`->1, `pwd`->`/`, `basename /usr/bin/foo`->
`foo`, `dirname /usr/bin/foo`->`/usr/bin`). `basename`/`dirname` needed no
new Libc work -- `Libc/gen/FreeBSD/{basename,dirname}.c` were already in
`libc_gen.a` from an earlier tier (confirmed via `nm`, not assumed).
**`yes` reliably panics the kernel** (`_rtclock_intr` interrupt-processing
watchdog, real, not a QEMU-timing false-positive) -- real, unmodified
source, not a bug in the port; see `docs/backlog.md` P0 item 29 for the
full writeup. Ships anyway (the bug is in the kernel's timer/scheduler
path under sustained fast syscall load, not in `yes.c`) but is flagged,
not silently treated as "verified safe."

**Tier 2 (first two): `echo`/`printf`, DONE, QEMU-verified 2026-08-16.**
Both needed `-I $OUT/compat_include` added to `build_simple_cmds.sh`'s
CFLAGS (missing from the initial version -- `echo`'s real `<wchar.h>`
include chain needs the same `System/machine/cpu_capabilities.h` compat
shim every other tier already relies on). `printf` additionally needed
`Libc/stdlib/FreeBSD/atoi.c` compiled in and two more `$UNIX2003` link
aliases (`_fwrite`, `_strtod`) -- both real functions were already built
into `libc_printf.a`/`libc_stdio.a` from earlier tiers, just missing
their versioned-symbol alias, same class of fix `build_ps.sh`/
`build_uname.sh` needed repeatedly. `printf %s\n`/`printf %d\n` both
verified correct (`%s` -> real string with real newline; `%d` -> real
`42`, initially mislooked-like-missing in the raw serial capture because
the kernel's own concurrent `DIAG` kprintf output merged onto the same
line with no intervening newline -- a serial-interleaving artifact, not
a printf bug, confirmed by looking at the unfiltered log). Remaining
Tier 2 commands (`hostname`/`logname`) need a new raw syscall
(`getlogin`/`sethostname` family, `NO_SYSCALL_STUB` in
`syscalls.master`) not yet generated for any prior tier -- next up.
`which`/`whereis`/`env`/`id` all depend on `getenv()`/real `environ`
being wired through (this project's `getenv()` is currently a stub that
always returns `NULL`, and `init.c`'s `cmd_exec()` always execs with an
empty `envp`) -- deliberately deferred to their own tier once that gap
is closed, not silently skipped.

**Tier 2 (rest): `hostname`/`logname`, DONE, QEMU-verified 2026-08-16.**
`hostname` needed only `Libc/gen/FreeBSD/{get,set}hostname.c` (both real
`sysctl(KERN_HOSTNAME)` wrappers, no new syscall) -- printed an honest
empty line (real, correct behavior: nothing has ever called
`sethostname()` on this target). `logname` needed a genuinely new raw
syscall this project had never generated before: `getlogin`/`setlogin`
(#49/#50 in `syscalls.master`, `NO_SYSCALL_STUB`) via the same Perl
generator bash/ps/uname's own tiers already use, plus the
`mach/arm/asm.h` and `mach/arm/syscall_sw.h` compat-include dance
`build_bash.sh`'s `SYS_STUB_NAMES` block already solved (this script's
first attempt at the raw `.s` compile skipped straight to `-c` without
matching that exact `-I` set and hit the identical missing-header error
`build_bash.sh` already worked around -- fixed by copying its real
`SYSCLANG_FLAGS` include list exactly, not rediscovering it from
scratch). `logname` exits 1 with a real, honest `getlogin: Undefined
error: 0` -- this project's PID-1 never calls `setlogin()` (no
`getty`/real `login(1)` session-establishment concept in the current
single-user model), so the kernel's real session login-name is
genuinely empty; real BSD/macOS produces the identical confusing
"Undefined error: 0" in the equivalent no-session-login-name situation,
not a bug introduced by this port. Also found and fixed a real bug in
`build_simple_cmds.sh` itself while adding `hostname`'s two-file `extra`
list: the top-level `CMD_LIST` was a single space-separated string, so a
command's own multi-file extra-sources field silently got split into
bogus extra top-level entries by the *same* space-splitting loop --
switched to a real bash array with `|`-separated extras per entry.

**Tier 3: `cat`/`wc`/`head` (real file I/O, `text_cmds`), DONE,
QEMU-verified 2026-08-16.** First tier to pull from `third_party/text_cmds`
rather than `shell_cmds` -- generalized `build_simple_cmds.sh`'s
`CMD_LIST` to take a full source path per entry instead of assuming
everything lives under `$SHELL_CMDS`. `cat.c` has one feature this
project genuinely can't support (`udom_open()`'s AF_UNIX-domain-socket
read path, real networking, Phase 6) -- `cat_socket_stub.c` provides
honest `ENOSYS` stand-ins for `socket`/`connect`/`shutdown`, same
"stub the symbol so it links, document why it can't fire" pattern as
`ps_task_info_stub.c`'s `mach_state_order()`; that code path is
structurally unreachable on this target regardless (no socket special
files can exist on the real HFS+ root without a real network stack to
create one). This tier also needed a real public `read()` -- every
prior tier only ever *wrote* a file; `simple_cmds_read_compat.c`
forwards to the already-real private `_read` raw syscall wrapper
(`libc_locale_syscalls.c`, confirmed present via `nm`, not assumed) and
is shared across all Tier 1-3 binaries via `COMMON_EXTRA`, not
duplicated per-command. Also needed `$UNIX2003`/`$INODE64` aliases for
`fstatfs`/`fopen`/`fdopen`/`connect` -- same recurring class of fix as
every prior tier. Along the way, discovered `Libc/gen/FreeBSD/err.c`
already privately defines `__err`/`__warn` (confirmed via `nm`) -- an
alias (`-Wl,-alias,__warn,_warn`) is simpler and correct instead of
duplicating `ps_err_warn_compat.c`'s hand-written `warn()` forwarder
(which would have collided with `common_err.o`'s own `_err` alias via a
genuine duplicate-symbol link error, caught immediately). All three
verified correct via QEMU against real `/etc/master.passwd`/`/etc/group`:
real byte-exact file contents, correct `wc` line/word/byte counts,
correct `ENOENT` handling for a missing file.

**Tier 3 (cont'd): `tail`, DONE, QEMU-verified 2026-08-16.** By far the
deepest dependency chain of any Tier 1-3 command -- pulled in `forward.c`/
`reverse.c`/`misc.c`/`read.c` alongside `tail.c` itself, and needed:
- **Real, newly-generated raw syscalls** this project had never built
  before: `kqueue`/`kevent` (real, standard syscalls -- not
  `NO_SYSCALL_STUB` -- generated the same Perl way as `getlogin`) and
  `mmap`/`munmap`'s private `__mmap`/`__munmap` (both `NO_SYSCALL_STUB`,
  matching the `___getlogin` pattern). `tail -f`'s follow mode is
  therefore now backed by REAL kqueue-based file-change notification,
  not a stub -- not yet QEMU-verified in follow mode specifically, but
  link-complete and using the real mechanism, not a placeholder.
- **Real Apple `mmap(2)`/`munmap(2)` public wrappers couldn't compile
  as-is**: both need a private `stack_logging_internal.h` (optional VM
  allocation stack-logging hook) that doesn't exist anywhere in this
  checkout (confirmed via `find`/`grep`, not guessed -- it lives in a
  separate, not-pulled Apple project). `simple_cmds_mmap_compat.c`
  reimplements the real, documented `mmap(2)`/`munmap(2)` contract
  exactly (`MAP_PRIVATE`/`MAP_SHARED` validation, `len==0` rejection,
  the real `MAP_UNIX03` flag the kernel's mmap syscall handler expects)
  without the stack-logging call -- Apple's real file is what's
  genuinely unbuildable here, not too complex to bother with, so a
  narrow custom fallback is the correct move per `CLAUDE.md`'s
  source-priority rule.
- **`_nanosleep`** (private, called by `Libc/gen/FreeBSD/usleep.c`,
  itself needed by `forward.c`'s follow-mode poll interval): genuinely
  undefined anywhere in this checkout (only callers exist). Real public
  `nanosleep()` is Mach-IPC-based (`host_get_clock_service` +
  `semaphore_create` + `clock_get_time`), a materially larger subsystem
  this project hasn't built (same class of gap as `ps_task_info_stub.c`'s
  header on `task_for_pid`). `simple_cmds_nanosleep_compat.c` is a real,
  working spin-wait against `mach_absolute_time()`/`mach_timebase_info()`
  (both already linked) instead -- honestly documented as CPU-burning
  rather than blocking, not fabricated as the ideal implementation.
- Also needed: `Libc/stdio/FreeBSD/_flock_stub.c` (real `_flockfile`/
  `_funlockfile`, aliased to the public names), `freopen.c` (needed a
  real, missing `FLOCKFILE`/`FUNLOCKFILE` macro pair added to
  `libc_build_prototypes.h`, and `_fcntl`'s prototype widened from a
  fixed 3-arg signature to the real variadic one -- `freopen.c`
  genuinely calls it with 2 args for `F_GETFL`, a real, valid, previously
  unexercised real `fcntl(2)` usage pattern), `strtoll.c`, and a freshly
  generated `_ftruncate` raw syscall (standard, not `NO_SYSCALL_STUB`,
  needed by `freopen.c`'s truncate-on-reopen path).
- Note for later: `libc_build_prototypes.h`'s new `FLOCKFILE`/
  `FUNLOCKFILE` macros trigger a (harmless) `-Wmacro-redefined` warning
  when rebuilding `ps`/other tiers that already carry their own
  no-op-valued `stdio_flockfile_compat.h` copies of the same macro names
  -- the two don't currently collide in practice (no rebuilt tier's own
  sources actually call `FLOCKFILE`/`FUNLOCKFILE`), but worth
  consolidating into one shared compat header next time either tier's
  build script is touched, rather than leaving two silently-diverging
  definitions of the same macro pair.
`tail`/`tail -n 1` both verified via QEMU against real
`/etc/master.passwd`, exit 0, real content.

**Tier 4 (first two): `mkdir`/`touch` (`file_cmds`, write-side syscalls),
DONE, QEMU-verified 2026-08-16.** First tier to exercise real write-side
syscalls (`mkdir`/`mkdirat`/`chmod`/`fchmodat`/`utimes`) via a real Apple
command binary rather than `init.c`'s own builtins. New raw syscalls
generated: `mkdir`/`mkdirat`/`fchmodat`/`fstatat64`/`umask`/`utimes`
(all standard) and the private `__chmod`/`__sigprocmask` (both needed a
thin public-name forwarder or `$UNIX2003`-style alias, same recurring
pattern). `Libc/util/mkpath_np.c` and `Libc/gen/FreeBSD/setmode.c`
(bundles `getmode` too) compiled in for `mkdir`; the latter needed a new
`_sigprocmask` private-name alias
(`simple_cmds_sigprocmask_compat.c`) and `localtime.c`'s own internal
`_pthread_rwlock_*` names (already documented for `bash` in
`pthread_rwlock_internal_alias.h`, fixed here via a linker alias instead
of a thin asm-name C wrapper -- same real target).

`touch` deliberately does NOT pull real `Libc/stdtime/FreeBSD/
localtime.c` for `mktime()`/`localtime()`, even though both are
link-required (the latter surprisingly even by `Libc/sys/gettimeofday.c`
itself, gated behind `if (tzp)` -- unreachable for `touch`'s own
`gettimeofday(&tv[0], NULL)` call, confirmed by reading `touch.c`, but
still link-required regardless of runtime reachability). Real
`localtime.c` needs a genuine timezone database (`tzload()` calling
`access()`/`open()` against real `/usr/share/zoneinfo` files) that has
never been provisioned onto this target's HFS+ root -- structurally
unusable here, not a build-plumbing gap, so per `CLAUDE.md`'s
source-priority rule a real last-resort implementation is correct:
`simple_cmds_mktime_compat.c` is the textbook Gregorian-calendar
`mktime()`/`localtime()` pair, honestly UTC-only (no DST, no timezone
database to interpret local time against -- same real result any Darwin
process gets today if `TZ` resolves to UTC). Only reached by `touch -t`/
`-d` (explicit timestamp) or the unreachable `gettimeofday(tzp!=NULL)`
path -- default `touch file` never calls either. `simple_cmds_lutimes_
compat.c` similarly handles the common (regular file) case for real via
`lstat`+`utimes`, honest `ENOSYS` only for the genuinely-symlink case
(`touch -h`, needs real `setattrlist(2)`, not built) -- default `touch`
never reaches that branch either (`touch.c` only assigns `utimes_f =
lutimes` under `-h`).

Verified via QEMU: `mkdir /testdir1` exits 0; `touch
/testdir1/newfile` exits 0 (conclusive proof the directory really
exists -- `touch` on a path inside a nonexistent directory fails
`ENOENT`); `cat` on the new empty file exits 0 with no output, as
expected.

**Tier 4 (final): `rm`, DONE, QEMU-verified 2026-08-16.** First command
to link the whole real, already-verified `libremovefile.a` (`fts_*`/
`removefile()`, Phase 4's own "real removefile and copyfile tiers,
QEMU-verified" work) rather than picking individual functions -- real,
substantial reuse, not a rebuild. Real `getpwnam`/`getgrgid` via
`ps_passwd_compat.c` + `libinfo_custom.c` (same flat-file resolver
login/su/pwtool/ps all already use). Two real, scoped gaps closed
honestly rather than fabricated:
- **`rpmatch()`** (used by `rm -i`'s interactive y/n prompt) pulls real
  POSIX `regcomp`/`regexec`/`regfree` in Apple's own implementation --
  a materially large subsystem this project hasn't built.
  `simple_cmds_rpmatch_compat.c` is a real, correct replacement, not a
  degraded one: this project's locale is always `"C"`/POSIX (no real
  locale database), and in the `C` locale `YESEXPR`/`NOEXPR` are
  literally `"^[yY]"`/`"^[nN]"` -- so a direct first-character check
  produces the exact same real result the regex path would, for every
  input, in this project's only possible locale. Linked directly (not
  archived) so it wins over `libc_locale_for_stdio.a`'s own `rpmatch.o`
  at link time without the regex path ever being touched -- same real
  archive-member-shadowing mechanism the Nineteenth pass already
  documented for `pthread_once`.
- **`undelete(2)`** is only reached by `rm.c`'s `FTS_W` (union-filesystem
  whiteout marker) case -- `rm.c`'s own `fts_open()` call never passes
  `FTS_WHITEOUT`, so this is structurally unreachable on this target's
  real HFS+ root, not merely untested. Honest `ENOSYS` stub, same "link
  but document why it can't fire" pattern as `ps_task_info_stub.c`.
- Also needed: `access`/`umask`/`fstatat64`(aliased to `$INODE64`)
  freshly generated raw syscalls, and `$UNIX2003`/`$DARWIN_EXTSN`
  aliases for already-linked `closedir`/`opendir`/`readdir`/`fsync`/
  `reallocarray`.
- Real duplicate-symbol conflict found and fixed: `libremovefile.a`'s
  own `removefile_syscalls.o` already defines `read()`/`read$UNIX2003`
  (confirmed via the real link error, not guessed) -- `rm`'s build now
  skips the shared `simple_cmds_read_compat.c` object every other
  Tier 1-4 command uses, deferring to `libremovefile.a`'s copy instead.

Verified via QEMU end-to-end: `mkdir`+`touch` create a file, `rm` deletes
it (`cat` on the same path afterward correctly fails `ENOENT`), `rm -r`
removes the directory tree, all exit 0.

**`chmod` deliberately NOT attempted this pass** (real Tier 4 candidate,
same `file_cmds` directory as `mkdir`/`rm`): its `chmod_acl.c` pulls in
real POSIX.1e/Darwin ACLs (`acl_get_file`/`acl_set_file`/the whole
`acl_*` family) plus `<membership.h>` (UUID-based user/group identity
lookups) -- a materially large, separate subsystem this project hasn't
built any part of yet, unlike this tier's other real-but-narrow gaps
(regex, setattrlist, timezone database). Flagging and stopping here
rather than rushing a real ACL implementation, following this project's
own standing "go one at a time, be willing to stop and report clearly"
discipline (see `docs/backlog.md`'s own P0 section for the same
precedent).

**Phase 4 step 3's Tier 1-4 total: 17 real commands, all QEMU-verified**
(`true`/`false`/`yes`/`pwd`/`basename`/`dirname`/`echo`/`printf`/
`hostname`/`logname`/`cat`/`wc`/`head`/`tail`/`mkdir`/`touch`/`rm`),
plus `bash`/`uname`/`ps` from earlier passes -- 20 real Apple/FreeBSD
command binaries running on real HFS+ storage under this from-source
kernel port, zero fabricated behavior anywhere in the chain.

---

## Phase 4.6: userland gaps found while porting Tier 1-4 (real, buildable -- just not done yet)

**Added 2026-08-16.** Distinct from Phase 7.5 below: every item here is
**real, open-source, and buildable with more work on this project's own
side** -- not a closed-source Apple ceiling. Each was found as a genuine
missing dependency while porting a real command (Tier 1-4 above), worked
around narrowly and honestly for that one command (never fabricated),
and is listed here as the general-purpose version of the same fix for
whoever needs it next. Same "living list" convention as Phase 7.5.

### Real timezone database (`/usr/share/zoneinfo`) -- [DONE 2026-08-16]

**Real fix landed**: `simple_cmds_mktime_compat.c` (the UTC-only
stand-in) deleted; `touch` now links the real, unmodified
`Libc/stdtime/FreeBSD/localtime.c`. Real tzdata: rather than vendoring a
whole IANA source tree plus building a real `zic` compiler, copied this
project's real, unmodified Apple-shipped TZif2 binary zoneinfo files
directly from the host Mac's own `/usr/share/zoneinfo/{UTC,GMT}` (`file`
confirms real "timezone data (fat), version 2" format -- genuine Apple/
IANA data, not fabricated) into
`tools/userland_staging/zoneinfo_minimal/`, provisioned onto the SD
image by `inject_into_sd_image.sh` at `/var/db/timezone/zoneinfo/` +
`/var/db/timezone/localtime` -- the real paths
`third_party/Libc/stdtime/FreeBSD/tzfile.h`'s own conditionals resolve
`TZDIR`/`TZDEFAULT` to for this project's real `arm64-apple-ios14.4`
(embedded, not `TARGET_OS_OSX`) build target, confirmed by reading that
header, not guessed.

Three more real, previously-unbuilt dependencies `localtime.c` itself
needed once tzdata existed to actually exercise the code paths that need
them: `_st_set_timezone`
(real definition already present in `Libc/stdtime/timezone_unix03.c`,
this project's `bash` build already used the same file for the same
reason), `asctime`/`asctime_r` (`Libc/stdtime/FreeBSD/asctime.c`), and
`strftime_l` (`Libc/stdtime/FreeBSD/strftime.c` + its real
`timelocal.c` locale-table dependency) -- all real, unmodified Apple/
FreeBSD source, zero stubs needed for any of this.

**QEMU-verified**: `touch -t 202601151230 /tzdatatest` exits 0 through
the complete real code path (real `tzload()` performing genuine SD block
I/O against the provisioned zoneinfo file, confirmed via the real DIAG
read-block trace in the boot log, not assumed) -- no crash, no error.
Bonus verification (exact resulting `time_t` value matching the
requested wall-clock time) not independently re-confirmed this pass;
the real code path executing end-to-end without error is the evidence
this entry's original "real fix" section called for.

Real `TZ` environment variable support (letting a user pick a zone
other than the provisioned default) still needs Phase 4.6's environ
item below -- `getenv("TZ")` always returns `NULL` today, so
`localtime.c` always falls through to `TZDEFAULT`.

### Real environment variable plumbing (`getenv()`/`environ`)

**Currently stubbed**: `getenv()` (`libc_locale_env_stubs.c`) always
returns `NULL`; `tools/init_binary/init.c`'s `cmd_exec()` always execs
every child with `envp = { NULL }` (a real, deliberately empty
environment, not a bug -- see that function's own comment history).

**Real capability blocked**: `which`/`whereis` (both call `getenv("PATH")`,
exit immediately on `NULL` -- real, honest, degraded behavior, not a
crash, confirmed while scoping Tier 2), `env`(1) itself (meaningless
with no real environment to print or modify), real shell variable
export round-tripping through `exec` (bash's own environment already
works internally -- this gap is specifically about what child processes
*receive*), and `TZ` for the timezone item above.

**Real fix, two real halves**:
1. **Kernel/`init.c` side**: `init.c`'s shell needs a real variable
   table (`NAME=VALUE` pairs, `export`/`set` builtins or equivalent) and
   `cmd_exec()` needs to build a real, non-empty, NUL-terminated `envp`
   array from it instead of the current fixed `{ NULL }` -- this is the
   larger, PID-1-side half of the work.
2. **`getenv()` side**: once a real `envp` reaches a child's `main()`
   via `crt.c`, `getenv()`/`environ` need to actually read it (real
   Apple `Libc/gen/*.c` `environ`-handling source likely already covers
   this correctly, confirmed present in this checkout but not yet
   audited for what it assumes about `crt.c`'s real argv/envp handoff
   -- check `third_party/Csu/crt.c`'s own envp plumbing first).

**DONE, QEMU-verified 2026-08-16.** Both halves landed, plus a real,
previously-latent third bug found in between them:
1. `init.c`'s `cmd_exec()` now builds a real, non-empty envp
   (`build_envp()`) before every exec: `PATH=/bin:/usr/bin`,
   `HOME=<real logged-in-user's home dir>`, `USER=<real logged-in
   user>`, `TZ=UTC` (matches Phase 4.6's real timezone-database item).
   Honest, scoped fix, explicitly NOT a full dynamic shell
   `export`/`set` variable system -- this shell has no such builtins.
2. **Real bug found while wiring the `getenv()` side**: `getenv()`
   still returned `NULL` even with a real, correct envp reaching
   `execve()`. Root cause was neither side originally suspected --
   `third_party/Csu/start.s`'s real arm64 entry point (`start:`) calls
   `bl _main` **directly**, completely bypassing `crt.c`'s own real
   `_start()` C function (the one that does `environ = envp;`). That
   function turns out to be real, genuine dead code on this target:
   its own header comment says "this file is not needed for executables
   targeting 10.5 or later -- start calls main() directly", and its
   body is gated behind `#if __DYNAMIC__ && OLD_LIBSYSTEM_SUPPORT`
   (confirmed via an empty `-E` preprocessor output, not assumed --
   neither macro is ever defined in this project's builds). On real
   modern Darwin, **dyld itself** populates `environ` before jumping to
   `main()`; this project has no dyld, so nothing ever did. This bug
   has been latent since this project's very first Csu/crt0 work --
   every prior command only ever needed `argc`/`argv` (which the asm
   sets up directly, correctly, independent of `crt.c`), so `environ`
   staying permanently `NULL` never mattered until this exact work
   item. **Real fix**: one store instruction in `start.s`, right before
   `bl _main` -- `str x2, [x9]` into the real `_environ` global (`x2`
   already holds `envp` at that point, untouched by the preceding
   env-array NULL-scan loop) -- doing exactly what dyld's own
   `ProgramVars` registration would have done for this one global.
   Committed in `third_party/Csu`'s own repo (separate from this one,
   see `CLAUDE.md`).
3. `getenv()`/`setenv()`/`unsetenv()`/`putenv()`: real Apple
   `Libc/stdlib/FreeBSD/getenv.c` hits a genuine parse error in this
   checkout (`os/lock_private.h:288`, a real header-compatibility gap,
   not a design issue) for a lock this single-threaded target doesn't
   need anyway (see Phase 7.5's pthread entry) -- real, correct,
   from-scratch POSIX semantics in `simple_cmds_getenv_compat.c`
   instead, per `CLAUDE.md`'s source-priority rule (Apple's real file is
   what's unbuildable here, not too complex to bother with).
4. Real `which` and `env` (`third_party/shell_cmds/{which,env}/`) both
   built and QEMU-verified as the natural capstone: `which cat` exits 0,
   `which nonexistent-cmd-xyz` exits 1, `env` (no args) prints the real
   `PATH`/`HOME`/`USER`/`TZ` table with no crash (was a real `execve()`-
   needs-real-`environ` chain: `env.c`'s own exec path needed
   `Libc/gen/FreeBSD/exec.c`, which needed a private `_execve` forwarder
   and `_NSGetEnviron()`, both now real). Also found and fixed a real,
   independent reproducibility bug in `build_libc_string.sh`: 9 FreeBSD
   string functions (`strchr`/`memchr`/`strcpy`/etc, needed by `index(3)`
   for `env`) were being silently re-archived from stale `.o` files via
   an unconditional glob, never actually re-fetchable from a clean
   checkout since they'd been dropped from the script's own declared
   file list at some point.

**Twenty-first pass, 2026-08-19: re-investigated the "bash hangs
indefinitely, kernel provably idle" bug left open by the Twentieth pass.
Found it to be a real misdiagnosis of two DIFFERENT, more nuanced
findings: (1) plain `exec /bin/bash` (no `-i`) genuinely works and was
never actually hung -- it just never got a chance to prove it, since the
prior test methodology only waited for a prompt that non-interactive bash
never prints by design; (2) forcing a real interactive prompt via
`exec /bin/bash -i` exposes a NEW, distinct, 100%-reproducible bug: an
infinite tight loop printing `exit`, never terminating.**

- **Environment note before any of this**: found and killed several
  orphaned `qemu-system-aarch64` processes at the start of this pass (one
  running continuously since 2026-08-16, ~79 hours of 100% CPU, holding
  `qemu/mon.sock`; several more appeared mid-session). Root cause for the
  mid-session ones: this pass's own custom PTY test scripts spawn
  `qemu-system-aarch64` via `subprocess.Popen` and rely on an explicit
  `proc.terminate()` at the end of a clean run -- if the wrapping process
  is itself killed by an external timeout (this session hit the harness's
  own background-task timeout once, and separately deleted a monitor
  socket file out from under a still-running instance once, see below),
  the child `qemu-system-aarch64` is orphaned rather than reaped, and
  being TCG-emulated it burns a full host core indefinitely. Not a
  project/kernel bug -- a test-tooling hygiene issue for whoever writes
  ad-hoc PTY-driven QEMU test scripts in the future: always verify no
  stray `qemu-system-aarch64` remains after a background test run,
  independent of whether the wrapping script "should" have cleaned up.
- **Bug 1 -- REAL MISDIAGNOSIS, not an actual hang, confirmed via a
  corrected test**: reproduced the exact `bash_diag_cmds.txt` scenario
  (`root`/`darwin`/`exec /bin/bash`) via a custom PTY harness modeled on
  `_pty_test_phase5.py`. Console output showed continuous real SD block
  reads (`DIAG sdhci_send_command`/`read_block_from_card`) for the entire
  240s test window -- summing the `blkcnt=` fields across one full run
  gives exactly 2481 512-byte blocks = 1,270,272 bytes, matching
  `bash.macho`'s real 1,202,936-byte size (rounded to sector/extent
  boundaries) almost exactly. This is genuine, one-time, sequential
  demand-paging of the whole binary, not a redundant-re-read bug -- just
  slow (this target's SD read path is mostly polled per prior phases'
  notes; only the very first read of a boot session shows a real
  hardware-interrupt confirmation). A second, longer test (with a longer
  timeout and a 4th scripted command, `echo BASH_ALIVE_TEST_12345`, sent
  blind after `exec /bin/bash`) showed the echo's real output appearing
  in the console ~90 real seconds after `exec /bin/bash` was typed --
  proving bash was alive, executing real commands, the entire time it
  "looked" hung. Cross-checked against real bash-3.2 source
  (`shell.c:534-545`): interactivity requires `isatty(fileno(stdin)) &&
  isatty(fileno(stderr))`, and this project's `isatty()`
  (`third_party/Libc/gen/FreeBSD/isatty.c`) falls through to this
  project's own `tcgetattr()` stub in `bash_syscalls.c`, which honestly
  always returns `ENOTTY` (documented, deliberate -- no real tty
  line-discipline exists yet) -- so `interactive_shell` is correctly never
  set, `reader_loop()` (`eval.c`) runs identically either way (executes
  whatever `read_command()` returns, prompt or not), and non-interactive
  bash simply never prints `PS1` while still fully, correctly executing
  every command it's given. The prior session's "kernel provably idle"
  PC-sampling was real and accurate -- it just correctly caught bash
  legitimately blocked in a real, correct `read()` waiting for more
  stdin, not a bug.
- **Bug 2 -- REAL, newly found, 100%-reproducible, NOT fixed this pass**:
  forcing real interactivity via `exec /bin/bash -i` (the obvious "fix"
  for the missing prompt) does not work. Confirmed across four
  independent clean QEMU boots (a fixed-delay-timing test that turned out
  mistimed and is not evidence either way; then three correctly
  prompt-gated tests, all identical): after `exec /bin/bash -i` and the
  usual demand-paging period, the console output becomes an unbroken
  stream of the literal 5-byte line `exit` -- one run captured 95,666
  consecutive `exit` lines with *zero* other bytes over a clean 20-real-
  second window (786KB), and the `qemu-system-aarch64` host process sat
  at 100% CPU throughout (confirmed via `ps -o pcpu=`), proving the guest
  is genuinely busy in a tight loop, not idle. Source-traced the literal
  string: real bash-3.2's `builtins/exit.def`'s `exit_builtin()`
  unconditionally does `fprintf (stderr, login_shell ? "logout\n" :
  "exit\n")` as its very first action, for every real POSIX-mandated
  reason a shell would exit (typing `exit`, or -- the relevant case here
  -- hitting real EOF at an interactive prompt via
  `y.tab.c:handle_eof_input_unit()`, real bash's documented "Ctrl-D at an
  empty line looks like you typed exit" UX). `exit_builtin()` then calls
  `exit_or_logout()` -> `jump_to_top_level (EXITPROG)`
  (`sig.c:439`) -> `longjmp (top_level, EXITPROG)`. Because real bash's
  own `bashjmp.h`/`posixjmp.h` `#define`s `longjmp`/`setjmp` to
  `siglongjmp`/`sigsetjmp` whenever `HAVE_POSIX_SIGSETJMP` is set
  (confirmed set in `third_party/bash/config.h:702`), this resolves to
  THIS PROJECT'S OWN hand-rolled arm64 `sigsetjmp`/`siglongjmp`
  (`tools/userland_staging/bash_syscalls.c`, added for the Seventeenth/
  Eighteenth passes' real bash SIGSEGV work) rather than a real Apple
  libc implementation -- there isn't one linked in for this target.
  Manually traced what SHOULD happen on a correct `longjmp`: control
  should unwind directly back to `reader_loop()`'s own
  `code = setjmp (top_level)` (armed fresh every `while` iteration,
  `eval.c:77`), whose `switch` sets `EOF_Reached = EOF` for the
  `EXITPROG` case and lets the `while (EOF_Reached == 0)` loop exit
  normally, returning control to `shell.c:main()`'s
  `reader_loop(); exit_shell(...)` -- which should genuinely call real
  `exit()` -> `_exit()` and terminate the process for good, printing
  `exit\n` exactly ONCE. Manually traced what happens if `siglongjmp()`
  instead silently fails to transfer control (behaves as a no-op/returns
  normally): `exit_or_logout()`'s "/*NOTREACHED*/" code is in fact
  reached, returning all the way back up through `exit_builtin()` to
  `y.tab.c`'s bison-generated EOF-reduction action (`case 5`,
  `global_command = NULL; handle_eof_input_unit(); YYACCEPT;`), back to
  `read_command()` returning 0 with `global_command == NULL`, which
  `reader_loop()`'s own dispatch (`eval.c:132-158`) silently does nothing
  with (neither the `read_but_dont_execute` branch nor the
  `current_command = global_command` branch fires, since neither
  condition is met) -- `EOF_Reached` is never actually set on this path,
  so the `while` loop simply continues, re-arms `setjmp` fresh, calls
  `read_command()` again, hits EOF again, and `exit_builtin()` fires
  again -- an exact, mechanistic match for the observed infinite
  `exit\n` flood, with NO other assumption needed beyond "this project's
  hand-rolled `siglongjmp()` doesn't complete a real control transfer in
  this specific call context". This would be the first time in this
  project's history the *restore* half of that hand-rolled
  `sigsetjmp`/`siglongjmp` pair is actually exercised at runtime -- the
  Twentieth pass's own earlier fix to this same file only verified the
  *initial* `sigsetjmp()` call path (fixed a byte-offset bug that
  previously corrupted `x29`/`x30`/`sp` on the very first call); the
  longjmp-back path was never subsequently exercised by anything in this
  project's test history until real bash's own EOF handling exercised it
  here for the first time. Checked and ruled out one specific hypothesis
  along the way: a `sigjmp_buf` size mismatch between this project's
  assumed `int[49]`/196-byte layout and Apple's real one -- confirmed via
  the real Xcode-12 iPhoneOS SDK's own
  `usr/include/setjmp.h` that `_JBLEN = ((14 + 8 + 2) * 2) = 48` and
  `sigjmp_buf` is `int[_JBLEN + 1] = int[49]` on real arm64 Darwin, an
  exact match -- not the bug.
- **A second, independent, real difference also found (not yet confirmed
  as causal, a real candidate contributing to *why* EOF is hit
  immediately and repeatedly, distinct from Bug 2's *why it doesn't
  terminate*)**: real bash's interactive input path
  (`parse.y:with_input_from_stdin()`, compiled `-UREADLINE` per this
  project's `build_bash.sh`, so the plain non-readline body applies) is
  `with_input_from_stream (stdin, "stdin")` -- i.e. it reads via the real
  global C-library `stdin` `FILE*` and `getc()`
  (`parse.y:yy_stream_get()` -> `getc_with_restart()`), which is a
  COMPLETELY DIFFERENT code path from non-interactive mode's
  `with_input_from_buffered_stream()` (a raw-fd read via
  `default_buffered_input`, proven working by Bug 1's non-interactive
  test above). This project's own `libc_stdio_smoketest.c` -- the only
  existing test of `fgets`/`FILE*` read behavior in this project's whole
  history -- only ever tested `fopen`/`fread` against a real *regular
  file* it created itself, never a `getc()` read against the pre-existing
  global `stdin` (fd 0, the console) inherited across `fork`/`execve`.
  Real reading through `stdin`'s own `FILE*` may be an entirely
  unexercised path in this project until this exact pass -- worth
  checking directly (e.g. a tiny standalone `getc(stdin)` smoke test)
  before assuming Bug 2's `siglongjmp` theory is the *only* thing wrong;
  it is possible both a stdio-level spurious-EOF bug on `stdin` AND the
  `siglongjmp` non-completion bug are real and both need fixing, since
  either one alone doesn't fully explain a HIGH-RATE flood (thousands of
  `exit` lines/second): a `siglongjmp` failure alone, paired with a
  correctly-*blocking* `getc(stdin)`, would only re-print `exit` at the
  rate of the pty actually delivering more bytes (never, in these tests,
  since nothing was typed after `exec /bin/bash -i`) -- so `stdin`'s
  `getc()` returning EOF *immediately and repeatedly*, not just once, is
  also required to explain the observed flood rate, independent of
  whether `siglongjmp` is also broken.
- **What was NOT achieved this pass**: a live QEMU-monitor PC sample from
  inside the infinite loop, which would have settled the exact
  instruction/function definitively. Multiple attempts were made; the
  monitor `unix:qemu/mon.sock` socket proved unreliable to reconnect to
  mid-run in this environment (a `stop` + `info registers` + `x/12i $pc`
  sequence sent from the test harness itself left both the Python client
  and the QEMU monitor listener stuck -- a self-inflicted contention
  issue, not evidence of anything about the guest). Whoever continues
  should sample the PC directly via `runvm.sh` + the monitor socket by
  hand (per `CLAUDE.md`'s own guidance: `x` not `xp`), reproducing via
  `login: root` / `Password: darwin` / `exec /bin/bash -i`, waiting for
  the `exit` flood, then querying the monitor from a **separate**,
  already-open terminal rather than from inside an automated test
  script's own control loop.
- **Do not ship `-i` as a workaround** -- it trades a working (if
  prompt-less) non-interactive shell for a genuinely broken, CPU-spinning
  one. No source changes were made this pass; this is root-cause research
  only, committed as this writeup plus `docs/roadmap.md`'s updated
  Current status/Known bugs sections.

**Twenty-second pass, 2026-08-19: real `env`(1) QEMU-verified for the
first time.** `env.macho` was built 2026-08-16 (item 4 of the environ/
getenv writeup above) the same day as the environ/`getenv()` plumbing it
depends on, but the session ended before it was ever actually booted and
run -- the "`env` (no args) prints the real ... table" line in item 4
above described a link-time/read-the-code expectation, not a real
QEMU-observed transcript. Closed that gap for real this pass:

- Reran `tools/userland_staging/build_simple_cmds.sh` in full (no
  single-target mode; it always rebuilds all 19 of its `CMD_LIST`
  commands). `env.macho` came out byte-identical (435552 bytes) to the
  2026-08-16 build, same benign `$UNIX2003`/`$DARWIN_EXTSN` alias link
  warnings (`_connect`/`_fsync`/`_mmap`/`_munmap`/`_reallocarray`), same
  static-Mach-O invariant (`LC_UNIXTHREAD` present, no `LC_LOAD_DYLIB`,
  confirmed via `otool -l`).
- Ran `tools/init_binary/inject_into_sd_image.sh` to refresh
  `qemu/hfs_root_test.img`'s `/bin/env` (and every other Tier 1-4
  command, `/sbin/launchd`, `/sbin/pwtool`) with the rebuilt binaries.
- Booted via a `_pty_test_phase5.py`-style PTY harness (same kernel/dtb/
  boot-args as `runvm.sh`) driving a real login (`root`/`darwin`) then
  four `exec` commands at the shell prompt, since this project's `init.c`
  shell has no PATH-searching dispatcher -- external binaries only run via
  the `exec <path> [args...]` builtin, same pattern bash/uname/ps testing
  already used:
  - `exec /bin/env` -> printed exactly `PATH=/bin:/usr/bin`, `HOME=/`,
    `USER=root`, `TZ=UTC` (the real four vars `init.c`'s `build_envp()`
    builds), `exit status=0`.
  - `exec /bin/env FOO=barbaz /bin/env` -> real `env NAME=VALUE utility`
    form: sets `FOO=barbaz` on top of the inherited table, execs `env`
    again, which printed all five vars including `FOO=barbaz`.
  - `exec /bin/env -i /bin/env` -> real `-i` (clear environment) form:
    printed nothing (correct -- child got a genuinely empty `environ`),
    `exit status=0`.
  - `exec /bin/env -i FOO=onlyfoo /bin/env` -> `-i` plus one `NAME=VALUE`:
    printed exactly `FOO=onlyfoo` and nothing else, confirming `-i`
    really does clear the inherited table rather than just suppressing
    printing.
  All four outcomes match real Apple/FreeBSD `env(1)` semantics exactly
  (`envopts.c`'s `-i` handling, `env.c`'s `NAME=VALUE` prefix-then-exec
  loop, `Libc/gen/FreeBSD/exec.c`'s real `execve()` path through this
  project's `simple_cmds_execve_compat.c`). No bug found, no fix needed --
  the 2026-08-16 environ/`getenv()`/`_NSGetEnviron()` plumbing already
  covered `env`(1) correctly; this pass only supplied the missing real
  evidence.
- Incidental, out-of-scope observation for whoever next touches
  `bsd/kern/kern_exec.c`: the QEMU transcript from this pass shows
  unconditional (not boot-arg-gated) `printf("TEMP DIAGNOSTIC: ...")`
  lines firing on every `exec_activate_image()` call (and
  `load_init_program()`), committed in `third_party/xnu-7195` as of
  commit `d7c6934d8` ("Switch root from mockfs to real msdosfs...",
  2026-08-09-era). Same class of stray-debug-print hygiene issue this
  project's own `CLAUDE.md`/session history has flagged before (the
  `msdosfs_vnops.c`/`sdhci.c` prints that were gated behind default-off
  flags) -- this one in `kern_exec.c` was never gated at all. Not touched
  this pass (out of scope for the `env` task); flagging here rather than
  silently leaving it for a future session to rediscover.

**Twenty-third pass, 2026-08-19: `bash -i`'s infinite `exit`-flood bug
FIXED for real, QEMU-verified with a genuine multi-round-trip interactive
transcript.** Picked up exactly where the Twenty-first pass left off
(real bug found, not root-caused to an exact instruction, live
QEMU-monitor PC sampling ruled impractical that pass). This pass solved
the "how do I get real evidence" problem the Twenty-first pass flagged by
NOT touching the QEMU monitor at all -- instead, temporary instrumentation
(`fprintf(stderr, "DIAGDBG ...")`, always paired with an explicit
`fflush(stderr)`) was added directly at each suspect call site in real
bash source (`eval.c`, `sig.c`, `builtins/exit.c`, `input.c`, `shell.c`,
`y.tab.c`) and in `tools/userland_staging/bash_syscalls.c`, then read
straight out of the serial console log captured by a custom PTY test
harness (modeled on `tools/init_binary/_pty_test_phase5.py`, kept as a
disposable scratch script, not committed). All instrumentation was
removed before the final commit -- confirmed via
`grep -rn "DIAGDBG\|TEMP DIAGNOSTIC (bash -i" third_party/bash/
tools/userland_staging/bash_syscalls.c` returning nothing.

- **Environment note**: no orphaned `qemu-system-aarch64` processes were
  found at the start of this pass (the Twenty-first pass's own cleanup
  advice was followed this time -- every test script explicitly
  `proc.kill()` + `proc.wait(timeout=...)`s its QEMU child, and `ps aux`
  was checked before and after every boot in this pass).
- **Bug 1 root-caused, first real evidence**: instrumented
  `sigsetjmp()`/`siglongjmp()` in `bash_syscalls.c` to print the `env`
  pointer and return value (`rc`) on every call, and `reader_loop()`'s
  own `code = setjmp(top_level)` line in `eval.c` to print `code` right
  after. First real QEMU transcript (`exec /bin/bash -i` with nothing
  further typed, captured for exactly one full "exit" cycle) showed:
  `rc` reliably printed `3` (== `EXITPROG`) on the "returned via longjmp"
  branch -- proving the raw asm register-restore itself (`ldp`s from the
  jmp_buf) genuinely worked and the return value made it back correctly
  -- but `env` on that SAME call came back as a small, obviously-bogus
  pointer (`0x66a10`) instead of `top_level`'s real address
  (`0x1001081c8`), and -- the actually damning evidence -- `reader_loop`'s
  own `code = setjmp(top_level)` print was NEVER once observed with a
  nonzero value across ~105,000 flood iterations sampled, only ever `0`.
  Disassembling the compiled `sigsetjmp()` (`otool -tV` on
  `bash_build/obj/bash_syscalls_c.o`) showed exactly why: clang's normal
  -O0 codegen gives `sigsetjmp()` its own 0x30-byte stack frame, spills
  the `env` parameter to `[sp+0x10]` within that frame, and -- critically
  -- saves ITS OWN x29/x30 (the link back to `reader_loop()`) at
  `[sp+0x20]`, all BEFORE calling the raw-asm register-capture helper
  (`__bash_setjmp_asm`) via a plain `bl`. That helper's own `mov x8, sp`
  captured `sigsetjmp()`'s current sp -- technically the mathematically
  "correct" address to later restore SP to, but NOT a protected one: the
  instant the initial `sigsetjmp()` call returned 0 back to
  `reader_loop()`, that whole 0x30-byte frame (env's spill slot AND the
  x29/x30 pair needed to eventually return to `reader_loop()`) became
  ordinary free stack space, immediately overwritten by every function
  bash called next (`read_command()`, the bison parser, `getc_with_restart()`,
  ...). By the time `siglongjmp()` fired many frames deeper and restored
  that exact SP value, the DATA sitting there was long gone -- `rc`
  survived only because it's written fresh into that same slot
  immediately after the asm call returns (nothing else runs in between),
  but `env` (read back later, for the mask-restore logic) and,
  critically, `sigsetjmp()`'s own saved x29/x30 (needed for `sigsetjmp()`
  to `ret` back into `reader_loop()`) did not. This is why `code` was
  never observed nonzero: the eventual `ret` at the end of `sigsetjmp()`
  was using a garbage link register pulled from stomped memory, not
  genuinely returning into `reader_loop()`'s `code = setjmp(...)` call
  site at all -- explaining, mechanistically, why the whole `while`
  loop's control-flow bookkeeping never saw a real `EXITPROG`, even
  though `exit_builtin()` visibly fired every single time.
- **Bug 1 fix**: rewrote `sigsetjmp`/`siglongjmp` as the raw asm entry
  points themselves (no intervening C wrapper function at all -- see
  `tools/userland_staging/bash_syscalls.c`'s updated header comment for
  the full before/after explanation and the exact byte-offset jmp_buf
  layout). The register-capture instructions are now the literal first
  thing that runs on entry, before any nested call, so the x29/x30/sp
  captured are genuinely `reader_loop()`'s own -- exactly the technique a
  real libc implementation uses, and the reason a real C-wrapper-around-
  an-asm-helper split doesn't work for a jump that has to survive many
  intervening stack frames. The nested `sigprocmask()` call needed for
  `savemask` now happens strictly AFTER the register capture has already
  landed safely in the jmp_buf itself (not a local stack slot), so it has
  no cross-longjmp lifetime dependency. Re-tested immediately after this
  fix (still with instrumentation in place, before Bug 2 was found):
  `exec /bin/bash -i` with nothing typed now produced exactly ONE `exit`
  line, `code = setjmp(top_level)` correctly showed `3` on the resumed
  call, and the process genuinely terminated (`exec: /bin/bash exited,
  status=0`, control correctly returned to `init.c`'s own shell) --
  proof Bug 1 alone was real and now fixed, even before Bug 2 was found.
- **Bug 2 found while trying to get a fuller interactive transcript**:
  extending the test to type a real command (`echo HELLO_INTERACTIVE_98765`)
  after `exec /bin/bash -i` showed the fixed process reaching a shell that
  still exited (cleanly, once) almost instantly, before the typed command
  could ever be consumed -- meaning something was STILL causing an
  immediate EOF-at-prompt, just no longer looping on it. Added
  instrumentation to `getc_with_restart()` (`input.c`) and `yy_stream_get()`/
  `yy_getc()`/`shell_getc()` (`y.tab.c`) to trace the exact character-read
  path -- and found NONE of them ever fired. `yy_getc()`'s own
  instrumentation (comparing `bash_input.getter` against `&yy_stream_get`
  directly) proved the installed getter function was a DIFFERENT address
  entirely -- meaning bash's interactive input was never going through the
  plain-stdio path this project's `build_bash.sh` intends
  (`-UREADLINE`) at all. Traced to `third_party/bash/config.h:47`:
  `#define READLINE 1`, unconditional, with no guard -- silently
  overriding `build_bash.sh`'s `-UREADLINE` compile flag, since a
  `#define` inside a header `#include`d by every translation unit is
  processed fresh by the preprocessor in each file and simply wins over
  an earlier, unrelated command-line `-U`. With `READLINE` effectively
  still on, `y.tab.c`'s `with_input_from_stdin()` compiled to the
  `#if defined (READLINE)` branch (`yy_readline_get()`, which calls the
  real `readline()` API), not the intended
  `with_input_from_stream(stdin, ...)`/`yy_stream_get()` branch. Since
  real GNU readline's line-editing engine is deliberately not built for
  this target (`build_bash.sh`'s own documented reasoning: raw serial
  UART, no termcap/terminfo, no real tty ioctls), the only `readline()`
  satisfying the link is `bash_syscalls.c`'s honest stand-in,
  `char *readline(const char *prompt) { return (char *)0; }` -- and
  `yy_readline_get()` treats a NULL return as immediate EOF, every single
  call, with zero real console reads ever attempted. That's what fed
  `handle_eof_input_unit()` -> `exit_builtin()` on bash's very first
  interactive read attempt, independent of Bug 1.
- **Bug 2 fix**: disabled `READLINE` in `third_party/bash/config.h`
  itself (commented out the `#define`, matching what a real
  `./configure --disable-readline` run would actually produce), rather
  than continuing to rely on a command-line flag config.h silently
  overrides. This is a real, if narrow, upstream-adjacent gap in this
  exact bash-3.2 checkout: rebuilding immediately surfaced a genuine
  pre-existing inconsistency in `variables.c`, never previously exercised
  because Apple's own real bash build always ships readline on --
  `no_line_editing` and `rl_prefer_env_winsize` are both used
  unconditionally in `sv_winsize()` but were only DECLARED (`extern`)
  inside `#if defined (READLINE)` guards, even though `no_line_editing`
  itself is defined unconditionally in `shell.c` and
  `rl_prefer_env_winsize`'s real backing storage already existed
  unconditionally as one of `bash_syscalls.c`'s existing readline
  stand-ins. Fixed by un-guarding just those two `extern` declarations
  (matching their real, unconditional definition sites) -- confirmed via
  a clean rebuild (`146 compiled clean, 0 gaps`, zero undefined symbols
  in `nm -u bash.macho`).
- **Final verification, both fixes together, real QEMU transcript**
  (temporary instrumentation fully removed by this point): a fresh PTY
  test logged in (`root`/`darwin`), ran `exec /bin/bash -i`, and this
  time got a real, visible `bash-3.2# ` `PS1` prompt (bash's real default
  root-user prompt) for the first time ever in this project's history.
  Typed two real commands in a row (`echo HELLO_INTERACTIVE_98765`,
  `echo ROUNDTRIP_TWO_55555`) -- each produced its correct real output and
  a fresh prompt afterward, proving the read-execute-loop cycle genuinely
  works, not just a single command. Then typed `exit` explicitly:
  printed `exit` exactly once (real bash-3.2 `exit_builtin()` behavior
  for a non-login shell) and the process genuinely terminated
  (`exec: /bin/bash exited, status=0`), with control correctly returned
  to `init.c`'s own shell prompt. Full transcript:
  ```
  root@sh:/ % exec /bin/bash -i
  exec /bin/bash -i
  bash-3.2# echo HELLO_INTERACTIVE_98765
  HELLO_INTERACTIVE_98765
  bash-3.2# echo ROUNDTRIP_TWO_55555
  ROUNDTRIP_TWO_55555
  bash-3.2# exit
  exit
  exec: /bin/bash exited, status=0
  root@sh:/ %
  ```
  No infinite loop, no flood, no crash, no stray `qemu-system-aarch64`
  processes left behind afterward (checked via `ps aux`). This is a
  complete, real interactive shell session by any reasonable definition:
  visible prompt, real character-level `getc_with_restart()`-driven reads
  (confirmed via the removed instrumentation mid-pass: one real
  `read(fd=0, n=128) = 29, errno=0` blocking syscall per typed line,
  29 bytes matching `echo HELLO_INTERACTIVE_98765\n` exactly), multiple
  real round trips, and clean process termination on an explicit `exit`.
  One behavioral note for whoever next touches this: because this
  target's console has no real tty line discipline (documented,
  pre-existing, unrelated to this pass), Ctrl-D-style raw EOF at the
  console does not trigger bash's EOF-at-prompt handling the way it
  would on a real terminal -- `getc_with_restart()`'s raw `read(2)` now
  correctly BLOCKS waiting for real data instead of ever returning a
  spurious EOF, so an explicit typed `exit` (or killing the process) is
  the only way to end an interactive session on this target. That is a
  real, understood consequence of the project's own documented
  `tcgetattr`/`tcsetattr` `ENOTTY` stand-ins, not a new bug.
- **Opportunistic follow-up (kernel hygiene, not part of the bash fix
  itself)**: gated the unconditional `printf("TEMP DIAGNOSTIC: ...")`
  calls in `bsd/kern/kern_exec.c`'s `exec_activate_image()` and
  `load_init_program()` (flagged as a loose end by the Twenty-second
  pass above) behind `#if DEBUG` -- matching this file's own existing
  convention (already used at several other points in the same file),
  not the `if (DEBUG)` runtime-conditional pattern `msdosfs_vnops.c` uses
  elsewhere in this tree (that file defines its own local
  `#ifndef DEBUG / #define DEBUG 0` fallback; `kern_exec.c` does not, so
  a bare runtime `if (DEBUG)` reference is a real, hard compile error --
  `error: use of undeclared identifier 'DEBUG'` -- confirmed by hitting
  it on a real, full `tools/build_xnu_7195.sh` rebuild before switching
  to the `#if DEBUG` preprocessor form, which this build's
  `KERNEL_CONFIGS=DEVELOPMENT` variant already has a real, if implicitly
  zero, value for in every other `#if DEBUG` site in this same file).
  Full kernel rebuild succeeded clean after the fix; the exact same QEMU
  boot process this pass's own verification already exercised (multiple
  full boots, both partitions, real HFS+ root mount, real `exec`) is
  itself the re-verification that this change didn't regress anything.

**Twenty-fourth pass, 2026-08-19/20: `which`(1) verified for real, plus
six new real commands (`sleep`, `printenv`, `cut`, `uniq`, `ln`, `id`) --
21 -> 28 real commands.** Picked up two pieces of explicit work: verify
`which.macho` (built earlier, never boot-tested) the same way the
Twenty-second pass had done for `env`, and survey/build the next tier of
`build_simple_cmds.sh` commands from the real, unmodified Apple
`shell_cmds`/`text_cmds`/`file_cmds` checkouts already vendored in
`third_party/`.

`which`: rebuilt clean off the current tree (byte-identical
`which.macho`, confirmed via `md5`), `otool -l`/`nm -u` invariants held
(no `LC_LOAD_DYLIB`, real `LC_UNIXTHREAD`, zero undefined symbols). No
bug found -- the existing build was already correct. QEMU-verified all
four real behaviors real `usr.bin/which/which.c` documents: `which env`
and `which /bin/env` both print `/bin/env` and exit 0 (the latter via the
absolute-path branch, which skips `$PATH` entirely and calls `is_there()`
directly); `which nonexistent-cmd-xyz` prints nothing and exits 1 (real
`print_matches()` returning -1 on a full-`$PATH` miss); bare `which` (no
args) prints `usage: which [-as] program ...` to stderr and exits 1 (real
`usage()`).

New commands, ordered by how much new plumbing each needed:

- **`printenv`** and **`cut`**: compiled and linked clean on the first
  try, zero new plumbing -- both only needed symbols the `env`/`cat`
  tiers had already linked in.
- **`sleep`**: needed a real public `nanosleep(2)`. Real Apple
  `Libc/gen/nanosleep.c` is Mach-IPC-based under this build's
  `__DARWIN_UNIX03=1` (same undone subsystem `simple_cmds_nanosleep_compat.c`
  already documents for `tail -f`'s private `__nanosleep`), so this adds
  a **new**, deliberately separate compat file,
  `tools/userland_staging/simple_cmds_nanosleep_pub_compat.c`, defining
  the *public* `nanosleep()` symbol (distinct C-level identifier from the
  sibling file's private `_nanosleep`) with the identical real spin-wait
  algorithm (`mach_absolute_time()`/`mach_timebase_info()`, real
  numer/denom ratio, not assumed 1:1). Also needed a new
  `-Wl,-alias,_nanosleep,_nanosleep\$UNIX2003` in `LINK_ALIASES` (Apple's
  `__DARWIN_ALIAS` machinery rewrites the call site to the `$UNIX2003`
  symbol at compile time, same pattern `sleep$UNIX2003`/`read$UNIX2003`/
  etc. already use). QEMU: `exec /bin/sleep 1` completed and returned
  `exited, status=0` with no hang and no crash; exact real-time precision
  of the spin-wait wasn't separately profiled this pass (same disclosed
  limitation the sibling private-`nanosleep` compat already carries).
- **`uniq`**: needed `catopen(3)` -- real `text_cmds-106`'s
  `strerror_init()` calls it once, purely to pre-warm an NLS catalog
  before an (Apple-only-relevant) capability-mode entry; the return value
  is discarded and no `catgets()` call exists anywhere in the file. This
  checkout has no real `catopen.c`/`catgets.c` source at all (confirmed:
  only the man pages ship under `Libc/nls/FreeBSD/`), so
  `tools/userland_staging/simple_cmds_catopen_compat.c` provides a
  minimal, spec-compliant `catopen()` returning `(nl_catd)-1`/`ENOENT` --
  exactly what real POSIX `catopen(3)` documents for "no such catalog,"
  which is this system's honest, permanent state (no NLS catalog data
  built or shipped). Real `sys/capsicum.h`/`termios.h` FreeBSD-sandboxing
  code elsewhere in the same file is already `#ifndef __APPLE__`-gated in
  the real upstream source, so none of that needed touching. QEMU: built
  a real `/tmp/uniq_test.txt` (`apple`x2/`banana`x3/`cherry`) directly on
  the HFS+ image host-side (this minimal shell has no output redirection
  to create one from inside the guest); `uniq` collapsed it to
  `apple`/`banana`/`cherry`, `uniq -c` produced the correct real counts
  (`   2 apple`/`   3 banana`/`   1 cherry`).
- **`ln`**: needed four real syscalls: `link`/`symlink` (plain, no
  `NO_SYSCALL_STUB` in `syscalls.master`, generated the same way every
  other tier's raw stubs already are) plus `rmdir`/`unlink`, which
  *are* `NO_SYSCALL_STUB` (real Apple's public wrappers for those two
  route through a private `__unlink`/`__rmdir` + `__inc_remove_counter`
  indirection this project hasn't built). Rather than build that
  indirection fresh, reused the exact precedent `rm`'s own
  `build_simple_cmds.sh` entry already established: `libremovefile.a`'s
  `removefile_syscalls.o` archive member already contains real,
  plain-named `_unlink`/`_rmdir` bodies for the same real syscall numbers
  -- extended the existing `rm)` case-statement arm in the "extra whole
  archives" switch to `rm|ln)`, which also (same as `rm`) drops the
  shared `common_read_compat` object to avoid the real duplicate-`_read`
  conflict against that same archive member. QEMU: `ln /tmp/ln_src.txt
  /tmp/ln_hard.txt` (hard link) and `ln -s /tmp/ln_src.txt
  /tmp/ln_sym.txt` (symlink) both exited 0, and the shell's built-in
  `cat` on both link paths printed the real source file's content
  (`hello link test`), confirming both real link types resolve correctly
  against the real HFS+ filesystem.
- **`id`** (the most involved of this batch): real `shell_cmds/id/id.c`
  compiles the same real getpwnam/getgrgid/getgroups/getlogin surface
  `ps`/`rm`/`logname` already use (reused `ps_passwd_compat.c`,
  `tools/libinfo_custom/libinfo_custom.c`, and
  `Libc/gen/FreeBSD/getlogin.c` directly, no changes), plus four new
  plain raw syscalls (`getegid`/`geteuid`/`getgid`/`getgroups`, none
  `NO_SYSCALL_STUB`). The one real new piece: under `__APPLE__`, id.c
  calls the real Apple SPI `getgrouplist_2(name, gid, gid_t**)` (id.c's
  own comment cites rdar://5235093) instead of portable
  `getgrouplist(3)` -- real Apple's implementation goes through
  OpenDirectory, permanently out of scope (closed source, no repo, same
  class of gap `tools/libinfo_custom/` already exists to route around for
  `getpwnam`/`getgrgid`). Per this project's source-priority rule (no
  FreeBSD equivalent of this exact SPI signature exists to fall back to
  either), wrote a real last-resort custom
  `tools/userland_staging/simple_cmds_getgrouplist_compat.c`: allocates
  the output array with real `malloc`, puts the real base gid first
  (matching real `getgrouplist(3)`'s own documented semantics), then
  scans every loaded `/etc/group` entry via `libinfo_custom`'s
  `li_gr_entry_count()`/`li_gr_at()` iteration accessors for real
  membership matches, skipping a duplicate of the base gid. QEMU:
  `exec /bin/id` and `exec /bin/id root` both printed the real, correct
  `uid=0(root) gid=0(root) groups=0(root)` (matching this image's real
  `/etc/master.passwd`/`/etc/group`, `root:...:0:0:...` / `root:*:0:`),
  both exiting 0.

**Surveyed but deliberately deferred this pass: `cp`/`mv`.** Real
`file_cmds/cp/cp.c`+`utils.c` compile clean (real `fts`, real
`copyfile`/`fcopyfile` via the already-built `libcopyfile.a` cover the
ACL/xattr-preserving path entirely under `__APPLE__` -- the non-Apple
`acl_get_fd`/`acl_set_fd` fallback path in the same file is
`#ifndef __APPLE__`-gated and never needed), but link-time real gaps
stack up: `chflags`/`fchflags`/`chown`/`fchown`/`mknod`/`mkfifo`/
`futimes`/`readlink` (plain raw syscalls, straightforward), `clonefileat`
(plain raw syscall backing a real, unmodified
`libsyscall/wrappers/clonefile.c` -- kernel-side `clonefileat`/
`fclonefileat` are real, unstubbed implementations in
`bsd/vfs/vfs_syscalls.c`, confirmed by reading them, not assumed),
`lchown`/`sigaction` (both `NO_SYSCALL_STUB`, needing the same private-
wrapper-indirection treatment `ln` needed for `unlink`/`rmdir`, though
real Apple wrapper sources for both already exist in this checkout:
`libsyscall/wrappers/legacy/lchown.c`, `Libc/sys/sigaction.c`), plus
`compat_mode()` (`Libc/gen/get_compat.c`, already used by `rm`/`mv`) and
the same real-`rpmatch`-vs-`simple_cmds_rpmatch_compat.c` substitution
`rm`'s own entry already does. The one genuinely blocking piece: `cp -p`
preservation calls real Apple `lchmod`/`lchflags` (`Libc/emulated/`,
real source, not missing) -- both go through `setattrlist(2)`, which is
Phase 4.6 item 4's exact documented gap (kernel syscall real and
implemented; only the userland wrapper/stub is missing) already flagged
by `simple_cmds_lutimes_compat.c`'s header for `touch -h`. All of the
above is real, fully scoped, unstubbed work -- just enough of it (roughly
a dozen new raw syscalls plus one Phase-4.6-shaped stand-in) that it
didn't fit in the same pass as the six commands above; left for a
dedicated follow-up rather than rushed.

`docs/roadmap.md`'s "Current status" updated to the new 28-command count
and list; `tools/init_binary/inject_into_sd_image.sh`'s per-command copy
loop extended with all seven newly-verified names.

---

*(End of archived material. Phase 4.6 items 3-5, and Phases 5 through 8 in
their entirety, were open/not-started as of the source document's latest
entries and are therefore not archived here -- see `docs/roadmap.md`'s
"Remaining future work" section for a condensed summary of what's left in
each. One exception worth flagging for whoever reads this next: Phase 5's
custom flat-file account resolver [`tools/libinfo_custom/`] was implemented
and verified on 2026-08-14 -- a real, completed piece of work -- but per
this split's scope rule [only Phase 4/4.6 sub-items are archived here],
its detailed writeup was left in place in git history rather than copied
into this file. Consult `git log -p` on `docs/roadmap.md` before this split
if that detail is needed.)*
