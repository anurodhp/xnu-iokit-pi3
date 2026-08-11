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
exist.

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
Real ARM64 IRQ delivery through the SDHCI interrupt path is still not
root-caused (a polling workaround is in place, see that memory) -- the one
open item from this phase's original scope. The rest of this section is
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
   in `docs/sd-and-ethernet-plan.md`.

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
