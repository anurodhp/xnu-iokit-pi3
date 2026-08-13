# Backlog

Open items found but not yet fixed, mostly surfaced by two adversarial
code reviews on 2026-08-13 (one on `BCM2837SDHCI`/the boot chain, one on
the rest of the custom IOKit kexts) plus a few longer-standing known gaps.
Organized by how much it matters, not by discovery order. File:line
references are as of commit `86b1653`; re-check before trusting them if
this list is stale.

Items already fixed this session (real IRQ delivery to `BCM2837SDHCI` via
the DT interrupt-cell bug, the broken hit-counter, ACMD41 CRC/index
flags, GPIO pinmux WiFi collision, the `pmap.c` real-hardware panic
regression, missing lazy hard-disable in both interrupt controllers,
`SMSC9514Ethernet`'s missing `IONetworkingFamily` clone, `BCM2837InterruptController`
mirror drift) are **not** repeated here — see `docs/roadmap.md`'s Phase 3
section and `iokit/BCM2837SDHCI/BCM2837SDHCI.h`'s header comment for that
history.

---

## P0 — real-hardware risk (would plausibly hang, storm, or panic a real Pi)

1. **`sdcard/`'s real-hardware image is stale and would boot none of this
   session's fixes.** `sdcard/rpi3.dtb` is a copy of `devicetrees/rpi3-realhw.dtb`,
   which is missing the `arm-io` `ranges` fix, `local-intc`'s
   `AAPL,phandle`/`interrupt-controller` marker, and `intc`'s
   `interrupt-parent` wiring that `devicetrees/rpi3.dts` (the QEMU-verified
   tree) already has — each independently documented in `rpi3.dts` as
   confirmed-fatal without it. `sdcard/kernel.development.bcm2837.unstripped`
   predates the driver work entirely. Regenerate `rpi3-realhw.dts` from
   `rpi3.dts` (the two should only differ in `dram-size`), recompile with
   `third_party/dtc-AppleDeviceTree/dtc` (**not** stock `dtc`), and copy the
   current kernel build into `sdcard/` before ever attempting a real boot.

2. **`BCM2837LocalIntc::handleInterrupt()` silently drops every pending bit
   except bit 8 (GPU IRQ).** `iokit/BCM2837LocalIntc/BCM2837LocalIntc.cpp`
   (`handleInterrupt`) reads `LOCAL_IRQ_PENDING0` once and only services the
   GPU passthrough bit. If firmware ever leaves a mailbox or local-timer
   source routed to this core (plausible on real hardware, structurally
   impossible in QEMU since `bcm2836_control_reset()` zeroes those there),
   the pending bit is never cleared, the CPU IRQ line stays asserted, and
   `sleh_irq()` re-enters forever — a silent hang with no panic.

3. **Nothing in this repo programs QA7's `LOCAL_TIMER_INT_CONTROL0`
   (`0x40000040`).** Repo-wide grep for that address / `timercontrol`
   finds zero writers — not the kexts, not `pexpert/`, not `osfmk/arm64/`.
   XNU arm64 takes the scheduler tick via **FIQ** (`sleh_fiq()`), not IRQ,
   and the generic timer's FIQ output is gated by this exact register in
   both QEMU and real hardware. Worth a runtime check (read back
   `0x40000040`, or confirm via QEMU monitor that `sleh_fiq` is ever
   entered) before assuming the "steady-state idle" boot claims generalize
   to real hardware — if this needs a write and doesn't have one, a real
   Pi would likely hang shortly after console init with no diagnostic.

4. **`BCM2837SDHCI::doAsyncReadWrite()` has no serialization at all** —
   no `IOCommandGate`, no workloop, no mutex. Two concurrent requests
   (now plausible: the SD image has two mounted partitions, `disk0s1` +
   `disk0s2`) can interleave register writes and both consume from the
   same `fPendingIntStatus`, causing one to eat the other's completion —
   silent data corruption or a hang, not a clean error. Survives today
   only because nothing currently issues overlapping I/O.

5. **No error recovery anywhere in `BCM2837SDHCI`.** Every error path is
   `IOLog(); return false;`. The SDHCI spec requires `SOFTWARE_RESET_CMD`/
   `SOFTWARE_RESET_DAT` after a command/data error and `CMD12` to abort an
   in-flight transfer; none are issued. After the first real CRC error,
   timeout, or card removal, `CMD_INHIBIT`/`DAT_INHIBIT` stay asserted and
   every subsequent command dies after a 1s busy-wait — the classic
   bring-up cliff QEMU's SD model never reproduces.

6. **`BCM2837SDHCI`'s SD clock never leaves ~490 kHz / 1-bit.**
   `setClock()` always writes divisor `0xFF` regardless of the requested
   rate (explicitly `// TODO` for the data-phase case), and
   `kRegHostControl` is never written to widen the bus. QEMU has no timing
   cost; on real hardware this is roughly 60 KB/s — mounting HFS+ and
   exec'ing a binary at that rate will look indistinguishable from a hang.

7. **No power-on ramp / 74-clock delay before `CMD0`**, and power is
   enabled *after* the clock in `start()` rather than before. Spec
   requires supply ramp + ≥1ms + 74 clocks first. Untested by QEMU.

8. **`kRegTimeoutControl` (`0x2E`) is declared and never written.** The
   data-timeout counter is left at its reset (shortest) value. QEMU
   doesn't implement `DATA_TIMEOUT_ERR` at all, so a real card that's
   merely slow could spuriously fail where QEMU never would.

9. **`kRegFIQControl` (intc `+0x0C`) is declared, never written.**
   `BCM2837InterruptController::start()` masks all IRQs but leaves
   `FIQ_CONTROL` at whatever firmware left it. QEMU resets it to 0; real
   firmware may not. Any source left FIQ-routed arrives at `sleh_fiq()`
   and gets misclassified as a timer tick.

10. **No memory barriers anywhere in `iokit/`.** `grep -rn "dsb\|dmb\|isb\|OSSynchronizeIO"` across the whole driver tree returns nothing —
    the only ordering primitive present (`OSMemoryBarrier()` in both
    interrupt controllers) guards CPU-to-CPU ordering, not MMIO ordering.
    Likely benign if `IODeviceMemory` mappings land Device-nGnRnE (typically
    already ordered for same-peripheral accesses), but unverified in this
    tree, and QEMU/TCG makes all MMIO strongly ordered regardless of
    whether real hardware would. Cheapest place to add one: after the
    `Enable_IRQs`/`Disable_IRQs` register writes in
    `BCM2837InterruptController::enableVector()`/`disableVectorHard()`,
    before the next `IRQ_PENDING` read.

11. **GPIO pinmux for the SD slot is still only ever exercised in QEMU**,
    where it's a documented no-op (the emulated card is parented onto the
    bus regardless of GPFSEL). This session's GPIO34-39-to-input fix (for
    the WiFi-collision bug) is logically sound but genuinely untested on
    real silicon — first real-hardware boot is the only way to close this.

---

## P1 — real correctness bugs (not hardware-specific, will eventually bite)

12. **`IOMediaBSDClient`'s lazy-singleton globals have an unguarded
    first-use race.** `third_party/xnu-7195/iokit/Storage/IOMediaBSDClient.cpp`
    (`IOMediaBSDClientGlobals_getInstance()`) does a bare
    `if (ptr == NULL) ptr = new ...` with no lock. The code comment already
    concedes the gap but rationalizes it as "expected to happen serially" —
    no longer true since `IOFDiskPartitionScheme` now produces 3 near-
    simultaneous `IOMedia` nubs (whole disk + 2 partitions), each spawning
    an `IOMediaBSDClient::init()` on a potentially different config thread.
    Two racing constructions means two major numbers / split minor tables —
    nodes from the losing instance are silently orphaned. Intermittent,
    would look exactly like "root device sometimes not found." Fix: make
    the allocation happen from a known-serial point, or guard with a lock.

13. **`BCM2837CPU` has no defensive `probe()` name re-check.** Every other
    DT-matched driver in this tree (`BCM2837ArmIO`, `BCM2837InterruptController`,
    `BCM2837LocalIntc`, `BCM2837SDHCI`) re-verifies `provider->getName()`
    in `probe()` as a defensive guard; `BCM2837CPU` doesn't. If it were
    ever to over-match (see item 20 for why the originally-cited reason for
    these guards was probably a misdiagnosis, but the guards themselves are
    still good practice), duplicate instantiation would drive
    `enabledCPUs` past `numCPUs`, permanently wedging
    `IOCPUInterruptController::registerInterrupt()`'s `thread_block()` wait
    for every future caller. Cheap to add for consistency alone.

14. **Ordering race: `BCM2837LocalIntc::start()` can block indefinitely** if
    it runs after `BCM2837CPU::initCPU()`'s `registerCPUInterruptController()`
    call but before its later `enableCPUInterrupt()` call.
    `lookUpInterruptController()` succeeds in that window while
    `enabledCPUs == 0`, and `IOCPUInterruptController::registerInterrupt()`
    (`third_party/xnu-7195/iokit/Kernel/IOCPU.cpp`) blocks on
    `thread_block()` until the later wakeup. `BCM2837CPU.h` acknowledges
    matching-order isn't strictly guaranteed but only reasons about
    `cpusRunning()`, not this specific window. Fix: reorder `initCPU()` so
    `registerCPUInterruptController()` happens after `enableCPUInterrupt()`.

15. **`tools/init_binary/init.c` (PID 1) has a few real rough edges**,
    lowest priority since it's bootstrap scaffolding Phase 4 will replace:
    - `read_line()`: a `sys_read` error/EOF makes `shell_main()`'s
      `if (n < 0) continue;` an unthrottled tight loop re-printing the
      prompt at 100% CPU on any console where input isn't polled (masked
      today by `serial=3` always being passed).
    - `init_main()`'s `sys_wait4()` return value is discarded, and there's
      no respawn-rate throttle — a child that dies instantly (e.g. `ECHILD`)
      would fork-loop unthrottled. Classic `init` rate-limits this.
    - The raw-syscall inline-asm wrappers (`syscall3`/`syscall4`/`sys_fork`/
      `sys_wait4`) don't mark the `is_error`/status output operand with an
      early-clobber (`&`) constraint. Currently safe (the compiler happens
      to consume inputs before the clobbered write), but fragile across a
      compiler upgrade — add `&` to be robust rather than lucky.
    - The boot banner still says `"booted via mockfs"` — stale since root
      moved to real HFS+ (`d7c6934d8`). Cosmetic, but it's the first thing
      a human sees.

16. **`BCM2837SDHCI::doAsyncReadWrite()`/PIO paths truncate `nblks` with no
    bounds check.** `writeReg16(kRegBlockCount, (uint16_t)nblks)` silently
    truncates requests ≥65536 blocks (32MB); nothing validates
    `block + nblks <= fBlockCount`, and no `kIOMaximumBlockCountReadKey` is
    advertised. Unlikely to be hit today, silent corruption if it is.

17. **`BCM2837CPU::getCPUName()` leaks an `OSSymbol` on every call** —
    `OSSymbol::withCString("cpu0")` returns a retained ref the caller
    doesn't release. Called rarely; cache it in a member instead.

18. **`SMSC9514Ethernet` has several real logic bugs**, all currently
    unreachable since the class has no personality and is never
    instantiated (no urgency, but worth fixing before it's wired up):
    - `smsc95xx_reset()`-equivalent code does blind register writes to
      `HW_CFG`/`PM_CTRL`/`MAC_CR` where Linux does read-modify-write,
      clobbering unrelated bits (`BIR`/`RXDOFF`/`MEF`, full-duplex/
      auto-speed defaults).
    - The PHY-reset loop doesn't check its own timeout flag before
      continuing as if it succeeded.
    - `mdioRead`/`mdioWrite` are never called from anywhere — the PHY is
      never initialized/auto-negotiated, yet `enable()` unconditionally
      sets `fLinkUp = true`.
    - `attachInterface()` is never called from `start()`, so no
      `IONetworkInterface` can ever be created — matches the "no
      personality, unreachable" state above.
    - `getPacketFilters()` advertises multicast filtering while
      `enablePacketFilter()` is a no-op — will break IPv6 ND the moment a
      transport exists.
    - `fInterface` is read without a retain/lock from a USB-completion
      context that can race `disable()` setting it to NULL — UAF shape.
    - `frameLen & 0x7FF` truncates silently with no length guard.

---

## P2 — known gaps / incomplete work (not bugs, just unfinished)

19. **Phase 3.5a still not done**: `tools/init_binary/init.c`'s `ls`/`cat`/
    `cd`/`pwd`/`mkdir` operate on a hardcoded in-memory fake tree, not real
    `open`/`read`/`getdirentries` syscalls against the actually-mounted
    root. Small, self-contained, no dependency on Phase 4.

20. **Phase 4's `Libc` build is unstarted** (`Csu`/crt0 and real syscall
    stubs are verified working — see `tools/userland_staging/` — but `Libc`
    itself, the actual C library, is not). Confirmed to be an Xcode-project
    build (`Libc.xcodeproj`, no plain Makefile) against a broken local
    `xcodebuild`; the real path is compiling a curated `.c` subset directly
    with `clang`, starting with string/mem functions (no syscall/TSD
    dependency) before `stdio`/`malloc`/locale. Expect the same
    header-skew-per-subsystem fight `IOStorageFamily`/`hfs` needed.

21. **`build_kexts.sh` only standalone-compiles 3 of 8 driver sources**
    (`BCM2837InterruptController`, `BCM2837SDHCI`, `SMSC9514Ethernet`) —
    `BCM2837CPU`, `BCM2837LocalIntc`, `BCM2837ArmIO`, `BCM2837Personalities`,
    `BCM2837PlatformExpert` are never syntax-checked outside a full kernel
    build, though all five are listed in `third_party/xnu-7195/iokit/conf/files`.
    Cheap to extend the `KEXTS` array (note `BCM2837Personalities.cpp`
    actually lives in `iokit/BCM2837ArmIO/`, not a same-named directory).

22. **`ls /` crash mentioned in old project memory needs re-verification.**
    An earlier session flagged `ls /` as crashing (real SIGSEGV-looking
    panic) after printing one entry. Recent boots in this session ran `ls`
    without incident, but that was likely still against the fake
    in-memory tree (item 19) rather than a real directory read — status
    unclear, re-check once 3.5a lands real syscalls.

---

## P3 — hygiene / cleanup

23. **Two headers overclaim a nonexistent IOKit matching-engine bug.**
    `iokit/BCM2837ArmIO/BCM2837ArmIO.h` and
    `iokit/BCM2837InterruptController/BCM2837InterruptController.h` both
    assert (one claims "empirically confirmed via instance-pointer-tagged
    logging") that `IOService::matchInternal()`'s `IONameMatch` handling
    over-matches personalities against sibling DT nodes. Tracing the real
    path (`IOPlatformDevice::compareName` → `IODTCompareNubName` →
    `CompareKey()`) shows an exact string match with no such defect. The
    actual bug in the commit that added these `probe()` guards
    (`c5fa3c6`) was a blind cast through the wrong vtable in
    `BCM2837ArmIO::start()` (since fixed, same commit) — a real, different
    bug that produced the same symptom. The `probe()` guards themselves are
    harmless belt-and-braces, worth keeping; just correct the stated
    reason in both header comments.

24. **Stray nested git repo at `iokit/.git/`** (own `config`/`HEAD`, no
    remote configured) inside a directory the parent repo tracks normally.
    Makes `git status` run from inside `iokit/` misleading/wrong. Probably
    a leftover from early scaffolding before the top-level repo existed —
    verify there's no uncommitted work in it, then remove it.

25. **~30 `TEMP DIAGNOSTIC` `printf`/`IOLog` calls remain in HEAD**, several
    on hot paths (`bsd/msdosfs/msdosfs_vnops.c` on every read/blockmap,
    `iokit/Kernel/IOService.cpp` on every `registerService`/`startMatching`).
    At real mini-UART baud rates (115200) each line costs milliseconds of
    synchronous busy-wait — combined with item 6's slow clock, this would
    make a working real-hardware boot look like a hang. Prune once no
    longer needed for active debugging.

26. **Missing bounds/length validation in both interrupt controllers**:
    `registerInterrupt()`/`enableVector()`/`disableVectorHard()` never
    range-check `vectorNumber` against `kNumVectors` (a malformed DT gives
    an OOB `&vectors[n]` write), and neither controller validates
    `fRegMap->getLength()` before indexing straight to a fixed offset.

27. **Resource leak on `start()` failure path** in both interrupt
    controllers: if `start()` fails after `fRegMap` is set, `stop()` is
    never called (IOKit convention) and `free()` doesn't release `fRegMap`
    either. Move the release into `free()`.

28. **`BCM2837CPU::quiesceCPU()`/`haltCPU()` are silent no-ops** — correctly
    hedged as fine for now (single core, no sleep/shutdown ever attempted),
    but will need real implementations before any power-management work.
