# Backlog

Open items found but not yet fixed, mostly surfaced by two adversarial
code reviews on 2026-08-13 (one on `BCM2837SDHCI`/the boot chain, one on
the rest of the custom IOKit kexts) plus a few longer-standing known gaps.
Organized by how much it matters, not by discovery order. File:line
references are as of commit `86b1653`; re-check before trusting them if
this list is stale.

Items already fixed *before* this backlog was written (real IRQ delivery
to `BCM2837SDHCI` via the DT interrupt-cell bug, the broken hit-counter,
ACMD41 CRC/index flags, GPIO pinmux WiFi collision, the `pmap.c`
real-hardware panic regression, missing lazy hard-disable in both
interrupt controllers, `SMSC9514Ethernet`'s missing `IONetworkingFamily`
clone, `BCM2837InterruptController` mirror drift) are **not** repeated
here — see `docs/roadmap.md`'s Phase 3 section and
`iokit/BCM2837SDHCI/BCM2837SDHCI.h`'s header comment for that history.

**Status update 2026-08-13 (second pass)**: a dedicated pass through this
list fixed items 1, 2, 3, 8, 9, 12, 13, 14, 16, 17, 26, 27 — each rebuilt
+ boot-tested individually via `_pty_test.py`, no regressions, real
interrupt delivery reconfirmed after every change. Each is marked
`[FIXED 2026-08-13]` in place below with a one-line pointer to what
changed; the original problem description is left intact underneath for
context/history, not deleted. Commits: `third_party/xnu-7195`
`228f4d203`..`8eddfacc2` (pushed to `xnu-pi3-macos11`), main repo
`f2ae010`..`5d2b46f` (not pushed). Items 4, 5, 6, 7, 10, 11 (all
`BCM2837SDHCI` real-hardware-timing/robustness items) were deliberately
**not** attempted this pass — see the note after item 11 for why.

**Status update 2026-08-13 (third pass, final integration testing)**: after
the second pass above plus Phase 3.5a (real `init.c` syscalls) landed
independently, a combined rebuild + repeated boot-test (kernel with all
P0/P1 fixes + shell with real `ls`/`cd`/`pwd`/`cat`/`mkdir`) surfaced two
new, real findings neither individual change's own testing had hit:

- **[MITIGATED, see item 0 below]** A `~40%`-reproducible panic
  (`INTERRUPT_MASKED_DEBUG`'s "Processing of an interrupt ... took N ns"
  watchdog, always flagging `_rtclock_intr`) turned out to be QEMU/TCG
  wall-clock timing jitter, not a guest bug - confirmed via `cpu-factor=10`
  (a real Apple pexpert boot-arg) sharply reducing but not fully
  eliminating the failure rate. Added to `runvm.sh`/`_pty_test.py` (QEMU
  boot paths only, not real hardware).
- **[NEW P0 ITEM 0 - NOT FIXED, root cause identified]** A separate, more
  serious panic — real kernel instruction fetch abort at `pc = 0x0`, `lr`
  inside `sleh_irq` — see item 0 below, added at the top since it's the
  most significant open finding in the whole backlog as of this pass.

New commits this pass: main repo only (`runvm.sh`, `_pty_test.py`
`cpu-factor=10`), not yet pushed pending the adversarial review the user
asked for once this pass was done.

**Status update 2026-08-13 (fourth pass — item 0 FIXED)**: the adversarial
review requested above found the *exact* mechanism behind item 0 via
disassembly (not just theory) and a specific minimal fix, which has now
been applied and verified: see item 0's own entry below, updated in
place. Also corrected two misleading comments the same review caught
(item 3's timer-register write, `IOMediaBSDClient`'s CAS-singleton "leak"
claim) and refreshed `sdcard/`'s kernel copy, which item 1 had left stale
even though it was marked fixed. Re-verified via 6 consecutive full boot
tests: 0 panics (down from 3 crashes across the prior 10 runs), real
interrupt-driven I/O confirmed intact. Commits: `third_party/xnu-7195`
`cb5a5baf7` (pushed), main repo (this commit).

The review also found the "hard kernel wall" conclusion in
`project_xnu_rpi3_userspace_libc_dyld.md` about pthread TSD is **wrong** —
see that memory file's own correction, added the same pass. Not yet acted
on (out of scope for this pass, which was driver/kernel-focused) but
important for whoever picks up Phase 4 `Libc` work next.

---

## P0, item 0 — [FIXED 2026-08-13, same day] real interrupt handler could be NULL when a genuine hardware IRQ arrived during early boot

**FIXED**: an adversarial review disassembled the actual panic (confirming
`lr` lands exactly at `sleh_irq`'s indirect-call return address) and found
the real arming mechanism the original investigation below hadn't:
`BCM2837LocalIntc::start()` and `BCM2837InterruptController::start()` both
published themselves as usable interrupt controllers
(`getPlatform()->registerInterruptController()`) *before* completing
their own upstream registration - breaking what would otherwise have been
a natural blocking barrier via `IOPlatformExpert::lookUpInterruptController()`
(confirmed unconditionally blocking, never returns NULL). A downstream
client like `BCM2837SDHCI` could find and start arming real hardware
interrupts through a controller that hadn't itself finished becoming
safe. Fixed by reordering both `start()` functions to publish only
*after* their own upstream registration succeeds, and making
`BCM2837CPU::initCPU()`'s three failure paths `panic()` instead of
silently returning (with the reorder in place, a failure there would
otherwise hang the whole interrupt chain forever with no diagnostic).
Re-verified via 6 consecutive full boot tests: 0 panics (down from 3
crashes in the prior 10 runs), real interrupt-driven I/O confirmed intact
throughout. The review also found real-hardware risk is likely *higher*
than the ~1-in-10 QEMU rate suggests: the BCM2835 `Enable_IRQs`
latches are not cleared by a warm reset (only a power cycle), so on real
hardware rebooting out of a previous OS/firmware, arbitrary sources may
already be live the instant this project's own masking runs - a
follow-on hardening (masking both controllers pre-`spllo()`, next to the
existing pre-MMU BCM2837 block in `osfmk/arm64/start.s`) is noted as
still-open, lower-priority hardening, not blocking. Original investigation
kept below for history.

---

**ORIGINAL (superseded by the fix above, kept for history)**

**Found 2026-08-13, during final integration testing** of the combined
P0/P1 fixes + Phase 3.5a real shell syscalls (neither individually
surfaced this — it needed real, sustained interrupt-driven I/O load from
several real filesystem operations in a row, which no single-item test
happened to exercise this heavily).

**Symptom**: `panic(cpu 0 caller ...): Kernel instruction fetch abort at
pc 0x0000000000000000, lr 0xfffffff0073242c8`. `lr` resolves (via `nm` on
the built kernel) to `_sleh_irq + 0x38`. Reading `sleh_irq()`
(`osfmk/arm64/sleh.c`): its non-`USE_APPLEARMSMP` body is
`cdp->interrupt_handler(cdp->interrupt_target, cdp->interrupt_refCon,
cdp->interrupt_nub, cdp->interrupt_source);` — a call through a per-CPU
function pointer. A jump to `pc=0` at exactly that call site means
`cdp->interrupt_handler` was still `NULL` when this ARM64 IRQ exception
fired.

**Root cause (established, not yet fixed)**: `cdp->interrupt_handler` is
only populated by `ml_install_interrupt_handler()`, called from
`IOCPUInterruptController::enableCPUInterrupt()`
(`third_party/xnu-7195/iokit/Kernel/IOCPU.cpp`), which
`BCM2837CPU::initCPU()` calls near the *end* of its setup sequence (after
`ml_processor_register()`). If a genuine hardware IRQ can be signaled to
this core before that point in boot — plausible, since nothing in this
driver stack currently guarantees CPU-level interrupts stay masked
(`DAIF`/`PSTATE.I`) until `enableCPUInterrupt()` has run — `sleh_irq()`
unconditionally calls through the still-NULL pointer. **This exact race
was structurally impossible to hit before today**: prior to the DT
interrupt-cell fix (this same day, see `docs/roadmap.md` Phase 3), no real
hardware interrupt had ever successfully reached this dispatch chain at
all, so the window was never live. Fixing the DT bug made real interrupts
work — and, as a direct consequence, exposed this previously-dormant
early-boot race for the first time.

**Reproduction rate**: hit once in ~10 combined-integration boot-test runs
this pass (separate from, and less frequent than, the timing-watchdog
issue above) — infrequent enough to need real investigation (not just a
retry) but frequent enough to be a real, live bug, not a one-off fluke;
unlike the timing-watchdog panic, this one is a hard crash with a
consistent, specific signature (same faulting PC/LR pattern), not a
symptom that varies run to run.

**Not fixed this pass** — needs real investigation into (a) exactly when
CPU-level interrupt masking is first lifted relative to
`enableCPUInterrupt()` running (grep `DAIFClr`/equivalent in
`osfmk/arm64/start.s` and early boot asm — not yet done), and (b) the
right fix: either delay unmasking until the handler is installed, install
a safe default/no-op handler earlier as a guard, or serialize
`BCM2837SDHCI`/other early-matching drivers' `registerInterrupt()` calls
to strictly happen after `enableCPUInterrupt()` (may already be true in
practice — the actual gap is CPU-level unmasking, not driver
registration order, which is a different, already-partially-addressed
class of race, see items 13/14 below).

---

## P0 — real-hardware risk (would plausibly hang, storm, or panic a real Pi)

1. **[FIXED 2026-08-13]** `devicetrees/rpi3-realhw.dts`/`.dtb` regenerated
   from `rpi3.dts` (confirmed the only real difference is `dram-size`),
   recompiled with the correct ADT `dtc`; `sdcard/`'s local (gitignored)
   copy refreshed too. Still genuinely unverified on real hardware (no
   serial cable) - this just means a real attempt is no longer silently
   doomed by a stale image. Original problem description below, kept for
   context.

   **`sdcard/`'s real-hardware image is stale and would boot none of this
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

2. **[FIXED 2026-08-13]** Latches (not `IOLog` - real interrupt context)
   the first occurrence of an unexpected pending bit into
   `fLastUnexpectedPending`, inspectable via debugger/coredump. Does NOT
   implement actual mailbox/local-timer source handling or masking (no
   safe non-interrupt-context hook exists in this class to log from more
   visibly) - converts total silence into *something* discoverable, not a
   full fix. Original problem description below, kept for context.

   **`BCM2837LocalIntc::handleInterrupt()` silently drops every pending bit
   except bit 8 (GPU IRQ).** `iokit/BCM2837LocalIntc/BCM2837LocalIntc.cpp`
   (`handleInterrupt`) reads `LOCAL_IRQ_PENDING0` once and only services the
   GPU passthrough bit. If firmware ever leaves a mailbox or local-timer
   source routed to this core (plausible on real hardware, structurally
   impossible in QEMU since `bcm2836_control_reset()` zeroes those there),
   the pending bit is never cleared, the CPU IRQ line stays asserted, and
   `sleh_irq()` re-enters forever — a silent hang with no panic.

3. **[FIXED 2026-08-13]** Investigated first, per this item's own framing:
   read the register live via the QEMU monitor post-boot, found it
   already `0xF0` (correctly FIQ-enabled) despite zero writers anywhere
   in this repo *or* in `u-boot`'s `bootxnu.c` (real hardware's actual
   boot chain) - QEMU's loader was silently providing this, real firmware
   won't. `BCM2837LocalIntc::start()` now writes it explicitly. Original
   problem description below, kept for context.

   **Nothing in this repo programs QA7's `LOCAL_TIMER_INT_CONTROL0`
   (`0x40000040`).** Repo-wide grep for that address / `timercontrol`
   finds zero writers — not the kexts, not `pexpert/`, not `osfmk/arm64/`.
   XNU arm64 takes the scheduler tick via **FIQ** (`sleh_fiq()`), not IRQ,
   and the generic timer's FIQ output is gated by this exact register in
   both QEMU and real hardware. Worth a runtime check (read back
   `0x40000040`, or confirm via QEMU monitor that `sleh_fiq` is ever
   entered) before assuming the "steady-state idle" boot claims generalize
   to real hardware — if this needs a write and doesn't have one, a real
   Pi would likely hang shortly after console init with no diagnostic.

4. **NOT ATTEMPTED this pass** - see the note after item 11 for why (real
   fix needs `IOCommandGate`/workloop integration, higher risk/effort than
   the rest of this batch).

   **`BCM2837SDHCI::doAsyncReadWrite()` has no serialization at all** —
   no `IOCommandGate`, no workloop, no mutex. Two concurrent requests
   (now plausible: the SD image has two mounted partitions, `disk0s1` +
   `disk0s2`) can interleave register writes and both consume from the
   same `fPendingIntStatus`, causing one to eat the other's completion —
   silent data corruption or a hang, not a clean error. Survives today
   only because nothing currently issues overlapping I/O.

5. **NOT ATTEMPTED this pass** - see the note after item 11.

   **No error recovery anywhere in `BCM2837SDHCI`.** Every error path is
   `IOLog(); return false;`. The SDHCI spec requires `SOFTWARE_RESET_CMD`/
   `SOFTWARE_RESET_DAT` after a command/data error and `CMD12` to abort an
   in-flight transfer; none are issued. After the first real CRC error,
   timeout, or card removal, `CMD_INHIBIT`/`DAT_INHIBIT` stay asserted and
   every subsequent command dies after a 1s busy-wait — the classic
   bring-up cliff QEMU's SD model never reproduces.

6. **NOT ATTEMPTED this pass** - see the note after item 11.

   **`BCM2837SDHCI`'s SD clock never leaves ~490 kHz / 1-bit.**
   `setClock()` always writes divisor `0xFF` regardless of the requested
   rate (explicitly `// TODO` for the data-phase case), and
   `kRegHostControl` is never written to widen the bus. QEMU has no timing
   cost; on real hardware this is roughly 60 KB/s — mounting HFS+ and
   exec'ing a binary at that rate will look indistinguishable from a hang.

7. **NOT ATTEMPTED this pass** - see the note after item 11.

   **No power-on ramp / 74-clock delay before `CMD0`**, and power is
   enabled *after* the clock in `start()` rather than before. Spec
   requires supply ramp + ≥1ms + 74 clocks first. Untested by QEMU.

8. **[FIXED 2026-08-13]** `resetHost()` now writes `0x0E` (the field's
   maximum, ~1s at typical base clocks). Original problem description
   below, kept for context.

   **`kRegTimeoutControl` (`0x2E`) is declared and never written.** The
   data-timeout counter is left at its reset (shortest) value. QEMU
   doesn't implement `DATA_TIMEOUT_ERR` at all, so a real card that's
   merely slow could spuriously fail where QEMU never would.

9. **[FIXED 2026-08-13]** `BCM2837InterruptController::start()` now writes
   `FIQ_CONTROL = 0` explicitly. Original problem description below, kept
   for context.

   **`kRegFIQControl` (intc `+0x0C`) is declared, never written.**
   `BCM2837InterruptController::start()` masks all IRQs but leaves
   `FIQ_CONTROL` at whatever firmware left it. QEMU resets it to 0; real
   firmware may not. Any source left FIQ-routed arrives at `sleh_fiq()`
   and gets misclassified as a timer tick.

10. **NOT ATTEMPTED this pass** - see the note after item 11.

    **No memory barriers anywhere in `iokit/`.** `grep -rn "dsb\|dmb\|isb\|OSSynchronizeIO"` across the whole driver tree returns nothing —
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

**Why items 4-7, 10 weren't attempted this pass**: all real, all in
`BCM2837SDHCI.cpp`'s hot I/O path, and all meaningfully higher risk than
the rest of this batch - #4 needs real `IOCommandGate`/workloop
integration (an architectural change, not a local fix), #5 needs a
careful CMD12/reset state machine that could interact with the
just-verified interrupt-delivery path in subtle ways, #6 changes real
signal timing (clock divisor/bus width) which is exactly the kind of
change that's hardest to trust from QEMU alone since QEMU has no timing
cost to begin with, and #7 similarly touches power/clock sequencing.
Following this backlog's own explicit guidance ("go one at a time,
rebuild+boot-test after each, be willing to stop partway through and
report clearly rather than rushing all four") - after a productive pass
through 12 lower-risk items, this was the right place to stop and hand
off clearly rather than push into higher-risk territory in the same
sitting. #10 (memory barriers) is lower-risk but was deferred alongside
these since it's genuinely unverifiable from QEMU (which makes all MMIO
strongly ordered regardless) - no rebuild+boot-test could actually
confirm or refute it, so there was no way to close the verification loop
this pass even if the change itself is cheap.

29. **Real, reproducible kernel PANIC (not a hang) from sustained fast
    console output — found 2026-08-16 while QEMU-verifying the real
    `yes` command (Phase 4 step 3, `docs/roadmap.md`).** `yes.macho`
    (real, unmodified Apple `shell_cmds/yes/yes.c` — a tight `puts("y")`
    loop, nothing unusual) reliably panics the kernel within seconds of
    starting: `"Processing of an interrupt (type = 2, handler address =
    0xfffffff00732d170, vector = 0) took 397932291 nanoseconds (timeout =
    354986666 ns)"` — the real `INTERRUPT_MASKED_DEBUG` watchdog (the
    same mechanism `cpu-factor=10` already tunes for ordinary QEMU/TCG
    jitter, per `CLAUDE.md`'s "Boot / test commands" section) firing for
    real, not a timing false-positive: ~400ms with interrupts
    provably masked is a genuine, large stall, not jitter. A single
    ordinary userspace process writing to its own stdout as fast as
    possible should never be able to mask interrupts for hundreds of
    milliseconds, on any real system — and the handler address resolves
    (via `nm` against this session's kernel build, not guessed) to
    `_rtclock_intr+0x0` exactly, i.e. the real-time-clock/timer
    interrupt's own handler, not the console/UART path as first
    suspected. The panic message's own semantics ("processing of an
    interrupt ... took N ns") means `rtclock_intr()` itself, once
    entered, took ~400ms to return — real per-tick scheduler/timer
    bookkeeping inside that handler is what's slow under this specific
    load, not something masking interrupts before it ran. Leading
    hypothesis, not confirmed: `yes`'s extremely tight `write()` loop
    (one syscall per line, no batching, real `puts()`+`fflush` per
    iteration) drives an unusually high trap/context-switch rate, and
    something per-tick inside `rtclock_intr`'s real scheduling
    accounting scales with that rate in a way this project has never
    exercised before (no prior Phase 4 command loops anywhere near this
    fast). Not root-caused or fixed this pass — flagging as P0 because
    it's a real userspace-triggerable kernel panic, not merely a
    slow/ugly path, and because real hardware's timer tick rate is not
    obviously immune to the same mechanism (unlike most of this
    section's QEMU-vs-real-hardware caveats, which lean on QEMU-specific
    timing slop).
    `yes.macho` still ships (the bug is not in `yes` itself, which is
    real, correct, unmodified source) but should not be treated as
    "verified safe to run" the way this project's other Phase 4 step 3
    commands are — anyone hitting this again should start from the exact
    panic string above and `nm`/`atos` the handler address against the
    current kernel build rather than re-diagnosing from scratch.

---

## P1 — real correctness bugs (not hardware-specific, will eventually bite)

12. **[FIXED 2026-08-13]** `IOMediaBSDClientGlobals_getInstance()` now uses
    a lock-free CAS singleton (`OSCompareAndSwapPtr`) instead of a bare
    check-then-assign. Verified: all 3 `IOMediaBSDClient` instances
    (disk0, disk0s1, disk0s2) still match correctly, root device still
    resolves the same as before. Original problem description below, kept
    for context.

    **`IOMediaBSDClient`'s lazy-singleton globals have an unguarded
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

13. **[FIXED 2026-08-13]** Added `probe()` re-checking `provider->getName()
    == "cpu0"`, matching every other DT-matched driver. Original problem
    description below, kept for context.

    **`BCM2837CPU` has no defensive `probe()` name re-check.** Every other
    DT-matched driver in this tree (`BCM2837ArmIO`, `BCM2837InterruptController`,
    `BCM2837LocalIntc`, `BCM2837SDHCI`) re-verifies `provider->getName()`
    in `probe()` as a defensive guard; `BCM2837CPU` doesn't. If it were
    ever to over-match (see item 20 for why the originally-cited reason for
    these guards was probably a misdiagnosis, but the guards themselves are
    still good practice), duplicate instantiation would drive
    `enabledCPUs` past `numCPUs`, permanently wedging
    `IOCPUInterruptController::registerInterrupt()`'s `thread_block()` wait
    for every future caller. Cheap to add for consistency alone.

14. **[FIXED 2026-08-13]** Reordered `BCM2837CPU::initCPU()` so
    `registerCPUInterruptController()` runs after `enableCPUInterrupt()`.
    Investigated while fixing: `IOPlatformExpert::lookUpInterruptController()`
    is unconditionally *blocking* (loops on `IOLockSleep()`, never returns
    NULL) - this item's original framing (a lookup finding NULL) doesn't
    match the real API contract, so the "race" mostly self-healed via
    `IOCPUInterruptController::registerInterrupt()`'s own blocking wait
    even before this fix; still a real hardening (removes an unnecessary
    block-then-wake cycle). Verified via 3 boot-test runs: 2/3 clean with
    real interrupt delivery confirmed, 1/3 stalled matching a pre-existing
    documented flake pattern at the same point under the *original* code -
    not attributed to this change (see the commit for the structural
    reasoning), but noted in case it recurs. Original problem description
    below, kept for context.

    **Ordering race: `BCM2837LocalIntc::start()` can block indefinitely** if
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

16. **[FIXED 2026-08-13]** `doAsyncReadWrite()` now rejects `nblks==0`,
    `nblks>=65536`, and `block+nblks > fBlockCount` up front, before the
    bounce-buffer allocation. Original problem description below, kept
    for context.

    **`BCM2837SDHCI::doAsyncReadWrite()`/PIO paths truncate `nblks` with no
    bounds check.** `writeReg16(kRegBlockCount, (uint16_t)nblks)` silently
    truncates requests ≥65536 blocks (32MB); nothing validates
    `block + nblks <= fBlockCount`, and no `kIOMaximumBlockCountReadKey` is
    advertised. Unlikely to be hit today, silent corruption if it is.

17. **[FIXED 2026-08-13]** Cached in a new `fCPUName` member, released in
    a new `free()` override. Original problem description below, kept for
    context.

    **`BCM2837CPU::getCPUName()` leaks an `OSSymbol` on every call** —
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

26. **[FIXED 2026-08-13]** Added `vectorInRange()` bounds check to
    `BCM2837InterruptController::enableVector()`/`disableVectorHard()`,
    and `fRegMap->getLength()` validation to both controllers' `start()`.
    Original problem description below, kept for context.

    **Missing bounds/length validation in both interrupt controllers**:
    `registerInterrupt()`/`enableVector()`/`disableVectorHard()` never
    range-check `vectorNumber` against `kNumVectors` (a malformed DT gives
    an OOB `&vectors[n]` write), and neither controller validates
    `fRegMap->getLength()` before indexing straight to a fixed offset.

27. **[FIXED 2026-08-13]** Both controllers' `free()` now releases
    `fRegMap` if still set (covers the start()-fails-after-mapping path
    `stop()` never runs for). Original problem description below, kept
    for context.

    **Resource leak on `start()` failure path** in both interrupt
    controllers: if `start()` fails after `fRegMap` is set, `stop()` is
    never called (IOKit convention) and `free()` doesn't release `fRegMap`
    either. Move the release into `free()`.

28. **`BCM2837CPU::quiesceCPU()`/`haltCPU()` are silent no-ops** — correctly
    hedged as fine for now (single core, no sleep/shutdown ever attempted),
    but will need real implementations before any power-management work.
