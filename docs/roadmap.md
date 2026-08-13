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
item from this phase's original scope -- is now CONFIRMED reaching
`BCM2837SDHCI::interruptOccurred()` (2026-08-13)**, once `BCM2837CPU`
(added later, see Phase 4-adjacent history) existed to wire
`sleh_irq -> IOCPUInterruptController -> BCM2837LocalIntc ->
BCM2837InterruptController -> BCM2837SDHCI` end to end -- it genuinely
didn't when this phase's polling workaround was first written. Added two
counters (`fRealInterruptHits`/`fPollFallbackHits`) to
`iokit/BCM2837SDHCI/BCM2837SDHCI.{cpp,h}` to tell the two paths apart from
a real boot log rather than guessing, and confirmed via `_pty_test.py`: a
real QEMU boot's CMD17 (single block read) data-transfer completion --
the actual I/O-latency-sensitive path -- was resolved by a genuine
hardware interrupt. **Not yet 100% of waits**, though: that same boot's
very first command (CMD0, issued immediately after
`BCM2837LocalIntc: registered against IOPlatformInterruptController`)
still resolved via the poll fallback -- plausibly a narrow startup race
before the interrupt chain is fully "hot" for the first command right
after registration, not re-broken delivery, but not root-caused further
since the existing hybrid poll+interrupt design in `waitForIntStatus32()`
already covers it correctly either way. **Decision: keep the hybrid
design, do not revert to a pure blocking wait** until interrupt delivery
is confirmed for 100% of waits, not just most of them -- reverting now
would risk a real hang on whichever wait doesn't get a real interrupt.
This closes out Phase 3's original scope; the rest of this section is
kept as historical record of the original plan.

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

## Phase 3.5: real VFS in the shell + partition the SD card for a real permission-bearing root (2026-08-10, 3.5b DONE, 3.5a next up)

Two small, independent items, both cheap relative to Phase 4, both worth
doing before committing to Phase 4's much bigger libSystem lift.

**3.5a -- make the current shell's filesystem commands real.**
`tools/init_binary/init.c`'s `ls`/`cat`/`cd`/`pwd`/`mkdir` currently
operate on an entirely fake, hardcoded in-memory tree (`nodes[]`/
`vfs_init()`) -- the file's own header comment says so outright. Real
syscalls are only used for `/dev/console` I/O and execing the binary
itself. Rewrite these five builtins to issue real `open`/`read`/
`getdirentries`/`fstat`/`mkdir` syscalls against the actually-mounted root
instead. Small, self-contained, no dependency on anything else in this
phase or Phase 4. **Not yet done** -- verified still-fake during the
3.5b boot test (`ls` after HFS root-mount still printed the old
hardcoded `bin/ sbin/ etc/ dev/ tmp/ var/` list, not the real HFS root's
actual `sbin/`+`dev/` contents).

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
currently-mounted filesystem's actual contents (3.5a, still open); a
2-partition SD card (FAT32 boot + `hfs` root) boots and mounts `hfs` as
root in QEMU (3.5b, done).

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

---

## Phase 5: multi-user -- real accounts, login, enforced permissions

**Depends on**: Phase 3.5 (real, permission-bearing `hfs` root -- without
it there is nothing for per-file ownership to attach to) and Phase 4
(needs `Libinfo` from the libSystem pull, and a real PID-1 capable of
spawning login sessions rather than being the shell itself).

Full research/detail already written to `docs/multiuser-userland-plan.md`
-- read that file directly, this section is a pointer + build order, not a
restatement. Headline components, all confirmed open source there: `login`/
`getty`/`crypt()`/`su`/`pwd_mkdb`/`vipw`/`chpass`/`passwd`
(`system_cmds`/`shell_cmds`/`Libc`), plus the `Libinfo`-backed flat-file
resolver from Phase 4 step 4 (`getpwnam`/`getpwuid`/`getgrnam` reading
`/etc/master.passwd`+`/etc/group` directly, FreeBSD's `pwd.c`/`pwd_mkdb` as
the reference, zero `opendirectoryd` dependency).

**Build order**: libSystem stack (Phase 4) -> `Libinfo` (file-only backend)
-> account-provisioning tools (`pwd_mkdb`/`vipw`/`chpass`/`passwd`) ->
custom PID-1 (Phase 4) spawning real `login`/`getty` sessions ->
`login`/`getty`/`crypt()` -> `su` -> real shell/coreutils (Phase 4) ->
PAM (later, OpenPAM as fallback if Apple's exact Darwin-19 tag can't be
confirmed) -> `sudo` (deferred indefinitely -- genuinely absent from
Apple's open-source tree; real macOS itself ships it as an independent
binary, not from `apple-oss-distributions`; `su` covers the minimal need).

**Exit criteria**: at least two distinct local accounts exist in
`/etc/master.passwd`+`/etc/group` on the real `hfs` root, each with a real,
separately-owned home directory; `login`/`getty` bring up a real
authenticated session per account (password-checked via `crypt()`); files
created by one user are inaccessible to the other per real, kernel-enforced
`hfs` permission bits (not just session-level identity, per
`multiuser-userland-plan.md`'s Option 3 being explicitly the *weaker*
interpretation this phase supersedes); `su` works between accounts.

---

## Phase 6: networking -- USB, Ethernet, DHCP, and basic net tools

Added 2026-08-11. Three strictly sequential sub-phases -- each is a hard
gate on the next, not parallelizable, because each layer is the wire
protocol/transport the next one runs over.

**Depends on**: Phase 4 (needs the libSystem stack -- `dyld`/`Libc`/
`Libinfo` -- and a real PID-1/coreutils environment for 6c's userspace
tools to run in; a DHCP client and `ifconfig` are userspace binaries, not
kernel code). Not blocked on Phase 5 (multi-user is orthogonal to
networking).

### 6a. USB host controller (DWC2)

Full plan already written: `docs/usb-support-plan.md` -- read that file
directly, this is a pointer, not a restatement. Headline: no DWC2 driver
exists anywhere accessible to this project (`IOUSBFamily` only ships
EHCI/OHCI/UHCI/XHCI); a real, working reference implementation is already
vendored in-repo at `third_party/u-boot/drivers/usb/host/dwc2.c` and is the
recommended port target; a minimal purpose-built stack (not a full
`IOUSBController` subclass) is that document's recommended scope, matching
the same narrow-interface pattern already used for `SMSCUSBTransport`.

**Exit criteria** (restated from that document): a real, parseable USB
device descriptor is read back from the LAN9514's Ethernet function
(behind its own internal hub port -- the chip is a hub+Ethernet combo, not
a plain dongle).

### 6b. Ethernet (SMSC LAN9514)

Full plan already written: `docs/sd-and-ethernet-plan.md`'s Ethernet
section -- read that file directly. Headline: `iokit/SMSC9514Ethernet`'s
chip-level protocol logic (reset sequence, MDIO/PHY access, MAC
programming, RX/TX framing) is already ported from Linux's
`drivers/net/usb/smsc95xx.c` and waiting behind the `SMSCUSBTransport`
abstract interface; that section is formally gated on 6a and stays that way
here -- do not resume this work until 6a's exit criteria are met.

**Exit criteria**: `SMSCUSBTransport` has a real (6a-backed) implementation,
`SMSC9514Ethernet` publishes a working `IOEthernetInterface`, and a raw
frame (e.g. an ARP request) can be sent and observed on the wire (QEMU: its
usermode/tap networking; real hardware: an actual link partner).

### 6c. DHCP client + basic net tools (`ifconfig`, `ping`, etc.)

**Not previously planned anywhere in this project** -- new for this
document. Depends on 6b (a working link-layer interface publishing packets
into the kernel's real BSD networking stack is a prerequisite for any of
this to have something to configure or address).

**Source, verified to exist and be tagged** (via `gh api
repos/apple-oss-distributions/<name>/tags`, same standard as every other
source citation in this project's docs -- not guessed):
- **`network_cmds`** (tags e.g. `network_cmds-741.100.2`) -- bundles
  `ifconfig`, `ping`/`ping6`, `netstat`, `arp`, `route`, `traceroute` and
  the rest of the standard BSD net-tool set as one repo, matching the
  `shell_cmds`/`file_cmds`-style bundling already used for Phase 4's
  coreutils pull.
- **`bootp`** (tags e.g. `bootp-534.120.2`) -- Apple's real DHCP
  client/lease-management code (internally referred to as
  "IPConfiguration"; there is no separate `IPConfiguration` repo -- checked,
  404s -- `bootp` is the actual repo name, don't go looking for the other
  name). This is a full daemon (`ipconfigd`) with an XPC-ish controller
  architecture in its later form -- too much for this project versus the
  option below, same reasoning as the PID-1/launchd tradeoff in Phase 4.
- **FreeBSD's `sbin/dhclient`** (`freebsd/freebsd-src`, no separate repo --
  it's a subdirectory of the monolithic FreeBSD source tree) -- **the
  recommended option, verified 2026-08-11 via `gh api
  repos/freebsd/freebsd-src/contents/sbin/dhclient`.** A fork of OpenBSD's
  privilege-separated dhclient (confirmed by the presence of
  `privsep.c`/`privsep.h`, OpenBSD's architecture -- FreeBSD dropped the
  older ISC-derived dhclient years ago; no `contrib/isc-dhcp` remains in
  the current tree, checked). BSD/ISC-licensed, plain fork/exec daemon
  (`dhclient.c` plus `options.c`/`tables.c`/`clparse.c`/`dispatch.c`/
  `bpf.c`/`packet.c`/`parse.c`/`privsep.c` -- roughly 200KB of C total),
  no Apple XPC/IPC machinery, same DHCP wire protocol as `bootp` either
  way. This is the exact same shape of decision already made for Phase 4's
  PID-1 (FreeBSD's plain BSD process model over launchd's Mach-port/XPC
  one) -- apply the same logic here rather than re-litigating it: prefer
  this over `bootp` unless something concrete about `bootp` turns out to
  be a better fit once actually read. One real port cost to account for:
  it uses BPF (`bpf.c`) for raw packet TX/RX, which needs *some* raw-socket
  or BPF-equivalent path to exist against this kernel's real BSD network
  stack -- confirm what this xnu checkout actually provides
  (`bsd/net/bpf.c` likely already exists in-tree as stock xnu source; not
  yet checked against this project's specific fork) before assuming
  `bpf.c` ports over unchanged.
- Tag selection: same Darwin-19-commit-date-proximity methodology as every
  other `apple-oss-distributions` pull in this project (see
  `docs/multiuser-userland-plan.md`/`reference_apple_oss_distributions`
  memory for the established method) -- don't assume a tag number without
  checking commit dates against the anchor `xnu-6153.11.26`.

**Concrete tasks, in order**:
1. Confirm 6b's `IOEthernetInterface` is visible to the real BSD
   networking stack as a normal `en0`-style interface (`ifconfig -a`
   equivalent at the kernel/sysctl level, before any userspace tool
   exists to show it).
2. Pull `network_cmds`, build `ifconfig` first (smallest useful tool,
   confirms real interface configuration -- link up, MTU, MAC -- works
   end to end before anything depends on a live network).
3. Pull FreeBSD's `sbin/dhclient` (recommended default per above -- verify
   `bpf.c`'s BPF dependency against this xnu fork's actual `bsd/net/bpf.c`
   first); fall back to `bootp` only if that dependency turns out to be a
   real blocker. Get a real leased IPv4 address and default route
   configured via whichever path is chosen.
4. Build `ping` (from the same `network_cmds` pull) as the actual
   end-to-end proof: a real ICMP echo round-trip over 6b's Ethernet
   driver, through a real DHCP-assigned address, off this board.
5. Round out with `netstat`/`arp`/`route` from the same `network_cmds`
   pull as they become useful for debugging the above, not as upfront
   scope.

**Exit criteria**: the board DHCP-leases a real IPv4 address on boot (or
via an explicit `ifconfig`+DHCP-client invocation), `ifconfig` shows a
real, correctly configured interface, and `ping` to an external host
succeeds -- the first real network traffic this project has ever sent or
received, kernel driver through to a userspace tool a person actually
types.

---

## Phase 7: closing the gap to a genuinely usable OS

Added 2026-08-11, after an explicit "is this a usable OS yet" gut-check
against Phases 1-6. Answer at the time: mostly, for a single-user embedded
Unix appliance, but with real gaps no phase covered -- no name resolution,
no remote access, no way to add software after the SD image is built. This
phase closes those. All sourcing below follows the project's standing rule
(see `feedback_source_priority_apple_freebsd_custom` memory): Apple's real
open-source version first, FreeBSD as fallback if Apple's is too complex or
closed, custom only if neither fits.

**Depends on**: Phase 6 (needs a real configured IP interface for 7a/7b to
have anything to resolve/listen against) and Phase 4 (libSystem stack,
real PID-1) for all three sub-phases. 7b additionally benefits from Phase 5
(real accounts) but doesn't strictly require it -- a single-user `sshd`
still has real value. Sub-phases are independent of each other and can be
done in any order once their shared dependencies land.

### 7a. DNS resolution

**Source, verified**: `libresolv` (`apple-oss-distributions/libresolv`,
tagged e.g. `libresolv-96`) -- real, open, `res_init`/`res_query`/
`gethostbyname` and friends, the actual gap Phase 6 left (`ping <ip>` works
today's plan, `ping <hostname>` doesn't without this). Tag-match to Darwin
19 per the established commit-date-proximity method.

**Concrete tasks**: pull `libresolv` at the matched tag; wire `/etc/resolv.conf`
parsing (populated by Phase 6's DHCP client -- FreeBSD's `dhclient-script`
already writes `resolv.conf` from DHCP option 6, confirm this still applies
once ported); confirm `getaddrinfo`/`gethostbyname` resolve a real hostname
through the new Ethernet link.

**Exit criteria**: `ping <hostname>` (not just `ping <ip>`) succeeds using
a DHCP-supplied DNS server.

### 7b. Remote access (`sshd`)

**Source, verified**: `OpenSSH` (`apple-oss-distributions/OpenSSH`, tagged
e.g. `OpenSSH-354.120.2`) -- real, Apple's actual shipped OpenSSH, BSD/ISC
licensed upstream. Apple-first rule applies cleanly here; no need to check
FreeBSD (FreeBSD also just ships upstream OpenSSH, no simplification to be
had by switching source).

**Real blocker to flag up front, not previously encountered at this
scale**: OpenSSH needs a real crypto backend (symmetric ciphers, MACs,
DH/ECDH key exchange, host-key signing) -- the same class of problem
already hit and solved narrowly for SHA-2 during bring-up (`g_crypto_funcs`
never registered because the corecrypto kext isn't available, worked
around with a from-scratch SHA-256/384/512 implementation, see
project memory). OpenSSH's crypto surface is much larger than SHA-2 alone
(AES, HMAC, at minimum one of ECDSA/Ed25519/RSA for host keys, a real DH
or ECDH implementation for key exchange). Check whether OpenSSH's own
vendored fallback crypto (it can build against a bundled minimal crypto
implementation when a system libcrypto isn't suitable -- verify this
against the actual pulled tag before assuming) covers this before writing
any of these primitives from scratch -- this is exactly the kind of case
the sourcing rule exists for: check what's already available (OpenSSH's
own bundled fallback) before defaulting to custom.

**Exit criteria**: `sshd` accepts a real TCP connection on port 22 and
completes a real SSH handshake + authenticated login from an external
client, to at least the single root account (full multi-account login can
wait on Phase 5 if that hasn't landed yet).

### 7c. Self-hosting toolchain

**Not free the way 7a/7b are** -- this is the largest item in this phase
and the one place the standard Apple-then-FreeBSD order doesn't produce a
clean "smaller" fallback, because FreeBSD's own base compiler is also
LLVM/Clang now (`contrib/llvm-project` -- checked; FreeBSD dropped its old
in-tree GCC years ago, so it isn't actually a simpler fallback here).

**Source, verified**: two real Apple options exist, of very different
size:
- **`clang`** (`apple-oss-distributions/clang`) -- Apple's actual current
  compiler. Matches "usable OS" most faithfully (same compiler regular
  Darwin uses) but is LLVM-scale -- millions of lines, its own multi-stage
  bootstrap, almost certainly impractical to self-host natively on-device
  for this project.
- **`gcc`/`gcc_42`** (`apple-oss-distributions/gcc`, tagged e.g.
  `gcc-5666.3`; also a separate `gcc_42` repo, not yet distinguished from
  the former) -- Apple's last GPLv2-era GCC (~4.2), from before Apple
  switched to Clang/LLVM as default. Substantially smaller and a much more
  realistic self-hosting target, at the cost of being a much older
  compiler (pre-Darwin-19 vintage; verify it can still produce valid
  binaries for this project's Darwin-19 ABI/`cctools`, don't assume).
  **Recommended starting point over `clang`** on pure size/feasibility
  grounds, consistent with picking the smaller/simpler option elsewhere in
  this project (e.g. `msdosfs` over `hfs` for the original SD boot, FreeBSD
  `init` over launchd) -- here the smaller option happens to still be an
  Apple one, not a FreeBSD one, which is fine; the rule is "smaller/open
  over larger/closed," not "FreeBSD over Apple" specifically.
- **`cctools`** (`apple-oss-distributions/cctools`, tagged e.g.
  `cctools-1035.1.102`) -- Apple's real assembler/linker (`as`/`ld`/
  `otool`/etc.), needed regardless of which compiler frontend is chosen
  since neither GCC nor Clang includes its own Mach-O backend for this
  target.

**Package management is deliberately not part of this sub-phase** --
originally scoped here as "genuinely unscoped," then resolved to a real
concrete plan built around FreeBSD's `pkg` once its custom-repo support and
unexpected native Mach-O awareness were found. See **Phase 8** below --
it's independent of this sub-phase (pkg installs prebuilt binaries; it
doesn't need an on-device compiler at all) and is not blocked on 7c
completing.

**Concrete tasks, in order**:
1. Pull `cctools` at a Darwin-19-matched tag; confirm `as`/`ld` produce
   binaries this kernel can actually load (should be low-risk -- this
   project's own kernel build already depends on a working Mach-O
   toolchain host-side).
2. Pull `gcc`/`gcc_42`, attempt a real self-hosted build (compiling a
   trivial C program *on the Pi itself*, not cross-compiled) as the
   concrete proof this sub-phase is trying to reach.
3. Only if `gcc_42` turns out to be incompatible with this project's
   actual Darwin-19 ABI, evaluate `clang` as a fallback despite its size
   cost -- don't start there.

**Exit criteria**: a C program written and compiled *on the board itself*
(not cross-compiled on a host and copied over) runs correctly.

### 7d. Text editor

**Source, verified**: `vim` (`apple-oss-distributions/vim`) -- real, and
covers this gap for free; no separate `ex`/classic-`vi` repo exists in
this org (checked, 404s) but `vim` includes `vi`-compatible mode, so this
isn't a real design decision, just a pull. Small enough to fold into
whichever of 7a-7c lands first rather than its own build order.

**Exit criteria**: a real file can be created and edited interactively via
`vim`/`vi` from the console.

**Overall Phase 7 exit criteria**: DNS-name-based `ping` works (7a); an
external `ssh` client can log into the board (7b); a program written on
the board itself can be compiled and run on the board itself (7c); a real
file can be edited on-device (7d). Installing new software
post-image-build is **not** part of Phase 7's exit criteria -- that's
Phase 8, and can land before, after, or in parallel with the rest of
Phase 7.

---

## Phase 8: package management (`pkg`)

Added 2026-08-11, split out of what was originally Phase 7c's
"genuinely unscoped" package-management note. Resolved during planning
discussion into a real, concrete plan once two things were checked
directly against `freebsd/pkg` rather than assumed:

1. **Pointing `pkg` at a fully custom repository is real, first-class,
   documented behavior**, not a workaround -- `pkg-repository.5`/
   `pkg-repositories.8` document the `/usr/local/etc/pkg/repos/*.conf`
   format (arbitrary `url:`), and `pkg-repo.8`/`pkg-create.8`/`pkg-key.8`
   are the actual tools for building and signing a repository from a
   directory of `.pkg` files -- exactly the "custom tap" shape this
   project needs.
2. **`pkg` already has native Mach-O awareness** -- `libpkg/pkg_abi_macho.c`
   (17296 bytes) and `libpkg/binfmt_macho.c` (11653 bytes), checked
   directly, are real substantial implementations, not stubs. `pkg`
   already models Mach-O as a first-class target ABI (used elsewhere for
   managing packages on macOS-family targets) -- nothing else evaluated
   for this project understood this project's own binary format out of
   the box. This meaningfully de-risks the whole phase versus building a
   package manager from nothing.

**Depends on**: Phase 4 (libSystem/real dyld-linked binaries -- `pkg`
itself is a real executable that needs a real userland to run in) and
Phase 6 (networking -- fetching packages from a repo needs a working IP
stack; DNS from Phase 7a helps but isn't strictly required if the repo URL
is an IP literal). **Not** dependent on Phase 7c -- `pkg` installs
*prebuilt* binaries; it doesn't need or use an on-device compiler. Can be
worked in parallel with any of Phase 7's sub-phases.

**Source, verified**: `pkg` (`freebsd/pkg` on GitHub, not
`apple-oss-distributions` -- Apple has no comparable open package manager
with dependency resolution; `.pkg`/`installer`/`pkgutil` are closed-ish
format tooling, not a real fallback here, so this is the sourcing rule's
FreeBSD tier applied cleanly). License: BSD-family ("Other" per GitHub's
detector, real permissive FreeBSD-project license, not GPL).

**Real, unavoidable constraint, same as every option considered for this
phase**: actual FreeBSD binary packages from `pkg.freebsd.org` will not
run here -- this fork's ABI (Darwin 19 / XNU / BCM2837 / ARM64) shares
nothing with FreeBSD's own userland ABI. Every package in this project's
custom repo has to be built specifically for this target, from source,
using this project's own toolchain (Phase 7c, or a host-side cross
toolchain if that's faster to stand up first). `pkg` doesn't remove this
cost -- no package-manager choice could -- it's just a much better-fitting
*vehicle* for it than Homebrew (no Ruby dependency) or a hand-rolled
format (gets real dependency resolution, signing, and Mach-O modeling for
free).

**Dependency porting cost, checked directly against `freebsd/pkg`'s
`external/` directory** -- `pkg` vendors its own copies of everything it
needs rather than assuming they're present on the host, which is good news
for porting (no ambient-system-library guessing) but each still needs to
actually build against this project's Libc/dyld:
- `libfetch` -- HTTP/FTP client, needed for repo/package fetching over
  Phase 6's network stack.
- `libsqlite` (bundled SQLite) -- local installed-package database. Among
  the most portable C codebases in existence (single amalgamated file,
  minimal syscall surface) -- low risk relative to the others here.
- `libucl` -- config/manifest parsing (repo catalogs, `pkg.conf`, package
  manifests all use this format).
- `libecc` -- elliptic-curve crypto for repo/package signature
  verification. Small and self-contained relative to a full TLS stack --
  **check for overlap with Phase 7b's OpenSSH crypto-backend work before
  porting twice**, both phases need real EC crypto primitives on a kernel
  that has no corecrypto kext (same class of gap already hit once for
  SHA-2 during bring-up, see project memory).
- `libblake2` -- BLAKE2 checksums for package integrity.
- `liblua` -- embedded Lua interpreter, used for `pkg`'s install
  scripts/triggers (`pkg-lua-script.5`/`pkg-triggers.5`). A real added
  language runtime, but Lua's reference implementation is ANSI C, small
  (tens of thousands of lines, not millions), and one of the most
  commonly cross-compiled/embedded interpreters that exists -- nowhere
  near the Ruby-for-Homebrew problem this phase's design deliberately
  avoided.
- `libelf`, `libyxml`, `liblinenoise`, `libder` -- smaller supporting
  libraries (ELF introspection, XML parsing, line-editing for `pkg shell`,
  DER encoding); lower individual risk, listed for completeness.
- `picosat` -- embedded SAT solver `pkg` uses internally for dependency
  resolution. Self-contained, no external deps of its own.

**Concrete tasks, in order**:
1. Port `libsqlite`, `libucl`, `libelf`, `libyxml`, `libder`, `picosat`
   first -- self-contained, no networking or crypto dependency, lowest
   risk, and enough to get `pkg`'s core building even before fetch/crypto
   work lands.
2. Resolve the `libecc` vs. Phase 7b crypto-backend overlap (see above)
   before porting either twice; port whichever's needed first.
3. Port `libfetch` against Phase 6's real network stack; confirm a real
   HTTP GET against an external host succeeds standalone before wiring it
   into `pkg` itself.
4. Port `liblua`; confirm a trivial script runs standalone before trusting
   `pkg`'s own Lua-based triggers.
5. Build `pkg` itself against the above; `pkg-create`/`pkg-repo`/`pkg-key`
   can run **host-side** (on the build machine, not the Pi) since they're
   only needed to produce and sign the custom repo, not to consume it --
   only the client-side `pkg install`/`pkg upgrade`/etc. paths need to run
   on-device.
6. Cross-build a first real package for this target (anything simple --
   e.g. a single already-ported coreutil, repackaged) using this
   project's own toolchain; publish it via `pkg create` + `pkg repo` to a
   repo this project controls (e.g. a directory served over plain HTTP, or
   a GitHub Pages/raw-content URL -- doesn't need to be anything elaborate
   to start).
7. Point an on-device `pkg` at that repo's URL via a custom
   `/usr/local/etc/pkg/repos/*.conf`; `pkg install` the package for real.

**Exit criteria**: a package built and published by this project (not
upstream FreeBSD) installs successfully via `pkg install` from a
custom-hosted repo onto the running board, and `pkg info`/`pkg delete`
correctly reflect and can remove it -- i.e. genuinely real, if
self-produced, package management, closing the last gap from the original
"is this a usable OS" review.
